// Simulator for 4 pipelined cores with private caches and MESI snooping bus.
// Implements 5-stage pipeline with decode-based hazard stalls and delay-slot branches.
// Caches are direct mapped, write-back, write-allocate; bus is single transaction per cycle with round-robin arbitration.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Architecture constants
#define NUM_CORES 4
#define REG_COUNT 16
#define IMEM_SIZE 1024
#define MAIN_MEM_WORDS (1 << 20)

// Cache parameters
#define CACHE_WORDS 512
#define CACHE_LINES 64
#define BLOCK_WORDS 8
#define OFFSET_BITS 3
#define INDEX_BITS 6
#define TAG_BITS (20 - OFFSET_BITS - INDEX_BITS)
#define INDEX_MASK ((1 << INDEX_BITS) - 1)
#define OFFSET_MASK ((1 << OFFSET_BITS) - 1)
#define TAG_MASK ((1 << TAG_BITS) - 1)

// Bus command values
#define BUS_NONE 0
#define BUS_RD 1
#define BUS_RDX 2
#define BUS_FLUSH 3

// MESI states
#define MESI_I 0
#define MESI_S 1
#define MESI_E 2
#define MESI_M 3

// Opcodes
enum {
    OP_ADD = 0,
    OP_SUB,
    OP_AND,
    OP_OR,
    OP_XOR,
    OP_MUL,
    OP_SLL,
    OP_SRA,
    OP_SRL,
    OP_BEQ,
    OP_BNE,
    OP_BLT,
    OP_BGT,
    OP_BLE,
    OP_BGE,
    OP_JAL,
    OP_LW,
    OP_SW,
    OP_HALT = 20
};

typedef struct {
    uint32_t raw;
    int op;
    int rd;
    int rs;
    int rt;
    int32_t imm;
    int pc;
} Instruction;

typedef struct {
    bool valid;
    Instruction inst;
} FetchStage;

typedef struct {
    bool valid;
    Instruction inst;
} DecodeStage;

typedef struct {
    bool valid;
    Instruction inst;
    int32_t rs_val;
    int32_t rt_val;
    int32_t rd_val;
} ExecStage;

typedef struct {
    bool valid;
    Instruction inst;
    uint32_t alu_result;
    uint32_t mem_addr;
    uint32_t store_data;
    bool is_load;
    bool is_store;
    bool miss;
    bool waiting;
    bool request_queued;
    uint32_t load_value;
} MemStage;

typedef struct {
    bool valid;
    Instruction inst;
    uint32_t value;
} WbStage;

typedef struct {
    // DSRAM: data words; TSRAM represented separately by tag/state arrays
    uint32_t data[CACHE_WORDS];
    uint16_t tag[CACHE_LINES];
    uint8_t state[CACHE_LINES];
} Cache;

typedef struct {
    // Stats counters collected per core and dumped to stats?.txt
    uint32_t cycles;
    uint32_t instructions;
    uint32_t read_hit;
    uint32_t write_hit;
    uint32_t read_miss;
    uint32_t write_miss;
    uint32_t decode_stall;
    uint32_t mem_stall;
} Stats;

typedef struct {
    int id;
    uint32_t imem[IMEM_SIZE];
    uint32_t regs[REG_COUNT];
    int pc;
    bool redirect_pending;
    int redirect_pc;
    bool stop_fetch;
    bool halted;
    bool done;
    FetchStage fetch;
    DecodeStage decode;
    ExecStage exec;
    MemStage mem;
    WbStage wb;
    Cache cache;
    Stats stats;
    FILE *trace_fp;
} Core;

typedef struct {
    bool active;
    int cmd;
    uint32_t addr;
    int origin;
} BusRequest;

typedef struct {
    int phase; // 0 idle, 1 wait (memory latency), 2 flush (streaming data words)
    int cmd;   // BUS_RD or BUS_RDX for current transaction
    int origin;
    uint32_t addr; // requested word address
    int shared;
    int provider; // 0-3 cache, 4 memory
    uint32_t block[BLOCK_WORDS];
    int delay;
    int index;
    // current cycle output
    int bus_cmd_out;
    int bus_origid_out;
    uint32_t bus_addr_out;
    uint32_t bus_data_out;
    int bus_shared_out;
} BusState;

// ---------- Utility helpers ----------

static int32_t sign_extend(uint32_t val, int bits) {
    uint32_t mask = (1u << bits) - 1;
    val &= mask;
    if (val & (1u << (bits - 1))) {
        val |= ~mask;
    }
    return (int32_t)val;
}

static Instruction decode_inst(uint32_t raw, int pc) {
    // Breaks the 32-bit word into opcode/rd/rs/rt/immediate and caches PC
    Instruction inst;
    inst.raw = raw;
    inst.op = (raw >> 24) & 0xFF;
    inst.rd = (raw >> 20) & 0xF;
    inst.rs = (raw >> 16) & 0xF;
    inst.rt = (raw >> 12) & 0xF;
    inst.imm = sign_extend(raw & 0xFFF, 12);
    inst.pc = pc;
    return inst;
}

static int dest_reg(const Instruction *inst) {
    // Returns architectural destination register index, or -1 if none
    if (inst->op == OP_HALT || inst->op == OP_SW)
        return -1;
    if (inst->op >= OP_BEQ && inst->op <= OP_BGE)
        return -1;
    if (inst->op == OP_JAL)
        return 15;
    if (inst->rd <= 1)
        return -1;
    return inst->rd;
}

static void source_regs(const Instruction *inst, int *out, int *count) {
    *count = 0;
    // Order of sources is irrelevant here; used only for hazard detection
    switch (inst->op) {
    case OP_ADD: case OP_SUB: case OP_AND: case OP_OR: case OP_XOR:
    case OP_MUL: case OP_SLL: case OP_SRA: case OP_SRL:
    case OP_LW:
        out[(*count)++] = inst->rs;
        out[(*count)++] = inst->rt;
        break;
    case OP_SW:
        out[(*count)++] = inst->rd;
        out[(*count)++] = inst->rs;
        out[(*count)++] = inst->rt;
        break;
    case OP_BEQ: case OP_BNE: case OP_BLT: case OP_BGT: case OP_BLE: case OP_BGE:
        out[(*count)++] = inst->rs;
        out[(*count)++] = inst->rt;
        out[(*count)++] = inst->rd;
        break;
    case OP_JAL:
        out[(*count)++] = inst->rd;
        break;
    default:
        break;
    }
}

// ---------- File helpers ----------

static void load_imem(const char *path, uint32_t *imem) {
    FILE *fp = fopen(path, "rt");
    if (!fp) {
        fprintf(stderr, "Failed to open %s\n", path);
        exit(1);
    }
    char line[128];
    int idx = 0;
    while (idx < IMEM_SIZE && fgets(line, sizeof(line), fp)) {
        unsigned int val = 0;
        sscanf(line, "%x", &val);
        imem[idx++] = val;
    }
    while (idx < IMEM_SIZE)
        imem[idx++] = 0;
    fclose(fp);
}

static void load_mem(const char *path, uint32_t *mem) {
    FILE *fp = fopen(path, "rt");
    if (!fp) {
        fprintf(stderr, "Failed to open %s\n", path);
        exit(1);
    }
    char line[128];
    int idx = 0;
    while (idx < MAIN_MEM_WORDS && fgets(line, sizeof(line), fp)) {
        unsigned int val = 0;
        sscanf(line, "%x", &val);
        mem[idx++] = val;
    }
    while (idx < MAIN_MEM_WORDS)
        mem[idx++] = 0;
    fclose(fp);
}

static void write_trimmed_mem(const char *path, const uint32_t *mem, int size) {
    FILE *fp = fopen(path, "wt");
    if (!fp) {
        fprintf(stderr, "Failed to open %s for write\n", path);
        exit(1);
    }
    int last = size - 1;
    while (last >= 0 && mem[last] == 0)
        last--;
    for (int i = 0; i <= last; i++)
        fprintf(fp, "%08X\n", mem[i]);
    fclose(fp);
}

static void write_full_mem(const char *path, const uint32_t *mem, int size) {
    FILE *fp = fopen(path, "wt");
    if (!fp) {
        fprintf(stderr, "Failed to open %s for write\n", path);
        exit(1);
    }
    for (int i = 0; i < size; i++)
        fprintf(fp, "%08X\n", mem[i]);
    fclose(fp);
}

static void write_regout(const char *path, const uint32_t *regs) {
    FILE *fp = fopen(path, "wt");
    if (!fp) {
        fprintf(stderr, "Failed to open %s for write\n", path);
        exit(1);
    }
    for (int i = 2; i < REG_COUNT; i++)
        fprintf(fp, "%08X\n", regs[i]);
    fclose(fp);
}

static void write_stats(const char *path, const Stats *s) {
    FILE *fp = fopen(path, "wt");
    if (!fp) {
        fprintf(stderr, "Failed to open %s for write\n", path);
        exit(1);
    }
    fprintf(fp, "cycles %u\n", s->cycles);
    fprintf(fp, "instructions %u\n", s->instructions);
    fprintf(fp, "read_hit %u\n", s->read_hit);
    fprintf(fp, "write_hit %u\n", s->write_hit);
    fprintf(fp, "read_miss %u\n", s->read_miss);
    fprintf(fp, "write_miss %u\n", s->write_miss);
    fprintf(fp, "decode_stall %u\n", s->decode_stall);
    fprintf(fp, "mem_stall %u\n", s->mem_stall);
    fclose(fp);
}

// ---------- Cache helpers ----------

static inline int cache_index(uint32_t addr) {
    return (addr >> OFFSET_BITS) & INDEX_MASK;
}

static inline uint32_t cache_tag(uint32_t addr) {
    return (addr >> (OFFSET_BITS + INDEX_BITS)) & TAG_MASK;
}

static inline uint32_t line_base_addr(uint32_t tag, int index) {
    return ((tag & TAG_MASK) << (OFFSET_BITS + INDEX_BITS)) | ((uint32_t)index << OFFSET_BITS);
}

static void writeback_line(Cache *c, int idx, uint32_t *mem) {
    // Write back dirty block before eviction
    if (c->state[idx] != MESI_M)
        return;
    uint32_t base = line_base_addr(c->tag[idx], idx);
    for (int i = 0; i < BLOCK_WORDS; i++) {
        uint32_t addr = (base + i) & (MAIN_MEM_WORDS - 1);
        mem[addr] = c->data[idx * BLOCK_WORDS + i];
    }
}

static void fill_cache_line(Cache *c, int idx, uint32_t tag, const uint32_t *block, int new_state, uint32_t *mem) {
    // Evict + fill helper used by bus completion
    writeback_line(c, idx, mem);
    for (int i = 0; i < BLOCK_WORDS; i++)
        c->data[idx * BLOCK_WORDS + i] = block[i];
    c->tag[idx] = tag & TAG_MASK;
    c->state[idx] = new_state;
}

static bool cache_lookup(Cache *c, uint32_t addr, int *state_out) {
    // Direct-mapped lookup, returns true on tag hit
    int idx = cache_index(addr);
    uint32_t tag = cache_tag(addr);
    if (c->state[idx] != MESI_I && c->tag[idx] == tag) {
        if (state_out)
            *state_out = c->state[idx];
        return true;
    }
    return false;
}

static uint32_t cache_read(Cache *c, uint32_t addr) {
    int idx = cache_index(addr);
    int offset = addr & OFFSET_MASK;
    return c->data[idx * BLOCK_WORDS + offset];
}

static void cache_write(Cache *c, uint32_t addr, uint32_t data) {
    int idx = cache_index(addr);
    int offset = addr & OFFSET_MASK;
    c->data[idx * BLOCK_WORDS + offset] = data;
}

// ---------- Bus helpers ----------

static void reset_bus_out(BusState *bus) {
    bus->bus_cmd_out = BUS_NONE;
    bus->bus_origid_out = 0;
    bus->bus_addr_out = 0;
    bus->bus_data_out = 0;
    bus->bus_shared_out = 0;
}

static void complete_transaction(BusState *bus, Core cores[NUM_CORES], uint32_t *mem) {
    // Flush completes: memory gets the block, requester cache filled
    if (bus->origin < 0 || bus->origin >= NUM_CORES)
        return;
    uint32_t base = bus->addr & ~(BLOCK_WORDS - 1);
    for (int i = 0; i < BLOCK_WORDS; i++) {
        uint32_t addr = (base + i) & (MAIN_MEM_WORDS - 1);
        mem[addr] = bus->block[i];
    }
    Core *c = &cores[bus->origin];
    int idx = cache_index(base);
    uint32_t tag = cache_tag(base);
    int new_state = (bus->cmd == BUS_RD) ? (bus->shared ? MESI_S : MESI_E) : MESI_M;
    fill_cache_line(&c->cache, idx, tag, bus->block, new_state, mem);

    if (c->mem.valid && c->mem.waiting) {
        c->mem.waiting = false;
        // allow mem stage to re-access without recounting miss
    }
}

static void apply_snoop(Cache *cache, int cache_id, int origin, int cmd, uint32_t addr, int *shared, int *provider, uint32_t *provider_block) {
    // Snooping reactions: invalidate/transition and optionally source data
    if (cache_id == origin)
        return;
    int idx = cache_index(addr);
    uint32_t tag = cache_tag(addr);
    int state = cache->state[idx];
    if (state == MESI_I || cache->tag[idx] != tag)
        return;

    *shared = 1;
    if (state == MESI_M) {
        *provider = cache_id;
        for (int i = 0; i < BLOCK_WORDS; i++)
            provider_block[i] = cache->data[idx * BLOCK_WORDS + i];
        if (cmd == BUS_RD)
            cache->state[idx] = MESI_S;
        else
            cache->state[idx] = MESI_I;
        // memory updated on flush later
    } else if (state == MESI_E) {
        cache->state[idx] = (cmd == BUS_RD) ? MESI_S : MESI_I;
    } else if (state == MESI_S && cmd == BUS_RDX) {
        cache->state[idx] = MESI_I;
    }
}

static void start_bus_transaction(BusState *bus, const BusRequest *req, Core cores[NUM_CORES], uint32_t *mem) {
    // Capture snapshot of request and decide data source (memory or peer cache)
    bus->cmd = req->cmd;
    bus->origin = req->origin;
    bus->addr = req->addr;
    bus->shared = 0;
    bus->provider = -1;
    bus->index = 0;
    uint32_t provider_block[BLOCK_WORDS] = {0};

    // snoop caches
    for (int i = 0; i < NUM_CORES; i++) {
        apply_snoop(&cores[i].cache, i, req->origin, req->cmd, req->addr, &bus->shared, &bus->provider, provider_block);
    }

    if (bus->provider == -1) {
        // served by memory
        bus->provider = 4;
        uint32_t base = req->addr & ~(BLOCK_WORDS - 1);
        for (int i = 0; i < BLOCK_WORDS; i++)
            bus->block[i] = mem[(base + i) & (MAIN_MEM_WORDS - 1)];
        bus->delay = 16;
        bus->phase = 1; // wait
    } else {
        // served by cache
        for (int i = 0; i < BLOCK_WORDS; i++)
            bus->block[i] = provider_block[i];
        bus->delay = 0;
        bus->phase = 1; // ready to flush next cycle
        bus->index = 0;
    }
    reset_bus_out(bus);
    bus->bus_cmd_out = req->cmd;
    bus->bus_origid_out = req->origin;
    bus->bus_addr_out = req->addr & ((1 << 20) - 1);
    bus->bus_shared_out = bus->shared;
}

// ---------- Tracing ----------

static void write_core_trace(int cycle, const Core *c) {
    if (!c->trace_fp)
        return;
    // Only dump a line when something is in flight in the pipeline
    bool active = c->fetch.valid || c->decode.valid || c->exec.valid || c->mem.valid || c->wb.valid;
    if (!active)
        return;

    char fbuf[4] = "---", dbuf[4] = "---", ebuf[4] = "---", mbuf[4] = "---", wbuf[4] = "---";
    if (c->fetch.valid)
        snprintf(fbuf, sizeof(fbuf), "%03X", c->fetch.inst.pc & 0x3FF);
    if (c->decode.valid)
        snprintf(dbuf, sizeof(dbuf), "%03X", c->decode.inst.pc & 0x3FF);
    if (c->exec.valid)
        snprintf(ebuf, sizeof(ebuf), "%03X", c->exec.inst.pc & 0x3FF);
    if (c->mem.valid)
        snprintf(mbuf, sizeof(mbuf), "%03X", c->mem.inst.pc & 0x3FF);
    if (c->wb.valid)
        snprintf(wbuf, sizeof(wbuf), "%03X", c->wb.inst.pc & 0x3FF);

    fprintf(c->trace_fp,
            "%d %s %s %s %s %s %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X\n",
            cycle, fbuf, dbuf, ebuf, mbuf, wbuf,
            c->regs[2], c->regs[3], c->regs[4], c->regs[5], c->regs[6], c->regs[7],
            c->regs[8], c->regs[9], c->regs[10], c->regs[11], c->regs[12], c->regs[13],
            c->regs[14], c->regs[15]);
}

static void write_bus_trace(FILE *fp, int cycle, const BusState *bus) {
    if (!fp || bus->bus_cmd_out == BUS_NONE)
        return;
    fprintf(fp, "%d %X %X %05X %08X %X\n", cycle, bus->bus_origid_out, bus->bus_cmd_out,
            bus->bus_addr_out & ((1 << 20) - 1), bus->bus_data_out, bus->bus_shared_out);
}

// ---------- Main simulation logic ----------

static int perform_compare(const Instruction *inst, int32_t rs, int32_t rt) {
    switch (inst->op) {
    case OP_BEQ: return rs == rt;
    case OP_BNE: return rs != rt;
    case OP_BLT: return rs < rt;
    case OP_BGT: return rs > rt;
    case OP_BLE: return rs <= rt;
    case OP_BGE: return rs >= rt;
    default: return 0;
    }
}

static uint32_t perform_alu(const Instruction *inst, int32_t rs, int32_t rt) {
    uint32_t shift = ((uint32_t)rt) & 0x1F;
    switch (inst->op) {
    case OP_ADD: return (uint32_t)(rs + rt);
    case OP_SUB: return (uint32_t)(rs - rt);
    case OP_AND: return (uint32_t)(rs & rt);
    case OP_OR:  return (uint32_t)(rs | rt);
    case OP_XOR: return (uint32_t)(rs ^ rt);
    case OP_MUL: return (uint32_t)(rs * rt);
    case OP_SLL: return (uint32_t)((uint32_t)rs << shift);
    case OP_SRA: return (uint32_t)((int32_t)rs >> shift);
    case OP_SRL: return (uint32_t)((uint32_t)rs >> shift);
    case OP_JAL: return (uint32_t)((inst->pc + 1) & 0x3FF);
    default: return 0;
    }
}

static void simulate(const char **files, uint32_t *main_mem) {
    Core cores[NUM_CORES] = {0};
    BusRequest requests[NUM_CORES] = {0};
    BusState bus = {0};
    int rr_next = 0;
    const char *limit_env = getenv("SIM_MAX_CYCLES");
    int max_cycles = -1;
    if (limit_env)
        max_cycles = atoi(limit_env);
    bool debug_branch = getenv("SIM_DEBUG_BRANCH") != NULL; // optional stderr logging for branch decisions

    // file order: 0-3 imem, 4 memin, 5 memout, 6-9 regout, 10-13 coretrace, 14 bustrace,
    // 15-18 dsram, 19-22 tsram, 23-26 stats
    for (int i = 0; i < NUM_CORES; i++) {
        cores[i].id = i;
        load_imem(files[i], cores[i].imem);
        cores[i].pc = 0;
        cores[i].regs[0] = 0;
        cores[i].regs[1] = 0;
        cores[i].trace_fp = fopen(files[10 + i], "wt");
        Instruction first = decode_inst(cores[i].imem[cores[i].pc], cores[i].pc);
        cores[i].fetch.valid = true;
        cores[i].fetch.inst = first;
        if (first.op == OP_HALT)
            cores[i].stop_fetch = true;
        cores[i].pc = (cores[i].pc + 1) & (IMEM_SIZE - 1);
        cores[i].decode.valid = false;
        cores[i].exec.valid = false;
        cores[i].mem.valid = false;
        cores[i].wb.valid = false;
    }
    load_mem(files[4], main_mem);
    FILE *bus_fp = fopen(files[14], "wt");

    // Each core owns a slot in requests[]; when a miss/upgrade happens MEM sets active=true and waits for arbitration.
    int cycle = 0;
    // Cycle order:
    // 1) Capture traces for current latch contents
    // 2) Commit WB writes
    // 3) Compute next-state for all pipeline stages (no forwarding)
    // 4) Arbitrate bus requests and drive bus outputs
    // 5) Advance bus timing (flush/latency)
    // 6) Check for completion/timeout
    while (1) {
        reset_bus_out(&bus);

        // trace before state changes (Q state of pipeline latches)
        for (int i = 0; i < NUM_CORES; i++)
            write_core_trace(cycle, &cores[i]);

        // WB stage: commit register writes and mark HALT retirement
        for (int i = 0; i < NUM_CORES; i++) {
            Core *c = &cores[i];
            if (c->wb.valid) {
                int dst = dest_reg(&c->wb.inst);
                if (dst >= 0)
                    c->regs[dst] = c->wb.value;
                c->stats.instructions++;
                if (c->wb.inst.op == OP_HALT)
                    c->halted = true;
            }
        }

        // pipeline advance (simulate combinational logic for next cycle)
        for (int i = 0; i < NUM_CORES; i++) {
            Core *c = &cores[i];
            if (!c->done)
                c->stats.cycles++;

            WbStage next_wb = {0};
            MemStage next_mem = c->mem;
            ExecStage next_exec = c->exec;
            DecodeStage next_decode = c->decode;
            FetchStage next_fetch = c->fetch;

            bool mem_advances = false;

            // MEM stage: handle cache access, misses enqueue bus requests
            if (c->mem.valid) {
                if (c->mem.waiting) {
                    // Waiting for bus transaction to complete
                    c->stats.mem_stall++;
                    mem_advances = false;
                } else {
                    Instruction *inst = &c->mem.inst;
                    if (inst->op == OP_LW || inst->op == OP_SW) {
                        bool counted = c->mem.miss;
                        int state = MESI_I;
                        bool hit = cache_lookup(&c->cache, c->mem.mem_addr, &state);
                        if (!counted) {
                            if (hit && state != MESI_I) {
                                if (inst->op == OP_LW)
                                    c->stats.read_hit++;
                                else
                                    c->stats.write_hit++;
                            } else {
                                if (inst->op == OP_LW)
                                    c->stats.read_miss++;
                                else
                                    c->stats.write_miss++;
                            }
                        }

                        if (!hit || state == MESI_I || (inst->op == OP_SW && state == MESI_S)) {
                            if (!c->mem.request_queued) {
                                requests[i].active = true;
                                requests[i].cmd = (inst->op == OP_LW) ? BUS_RD : BUS_RDX;
                                requests[i].addr = c->mem.mem_addr & ((1 << 20) - 1);
                                requests[i].origin = i;
                                c->mem.request_queued = true;
                            }
                            next_mem.miss = true;
                            next_mem.waiting = true;
                            c->stats.mem_stall++;
                            mem_advances = false;
                        } else {
                            if (inst->op == OP_LW) {
                                next_mem.load_value = cache_read(&c->cache, c->mem.mem_addr);
                                next_wb.valid = true;
                                next_wb.inst = *inst;
                                next_wb.value = next_mem.load_value;
                                next_mem.valid = false;
                                mem_advances = true;
                            } else {
                                cache_write(&c->cache, c->mem.mem_addr, c->mem.store_data);
                                if (state == MESI_E)
                                    c->cache.state[cache_index(c->mem.mem_addr)] = MESI_M;
                                next_wb.valid = true;
                                next_wb.inst = *inst;
                                next_wb.value = 0;
                                next_mem.valid = false;
                                mem_advances = true;
                            }
                        }
                    } else {
                        next_wb.valid = true;
                        next_wb.inst = *inst;
                        next_wb.value = c->mem.alu_result;
                        next_mem.valid = false;
                        mem_advances = true;
                    }
                }
            }

            bool mem_free_next = (!c->mem.valid) || mem_advances;
            bool exec_can_move = c->exec.valid && mem_free_next;
            bool exec_free_next = (!c->exec.valid) || exec_can_move;

            // EXEC stage: execute ALU or compute addresses, then hand to MEM
            if (c->exec.valid && exec_can_move) {
                Instruction *inst = &c->exec.inst;
                next_exec.valid = false;
                next_mem.valid = true;
                next_mem.inst = *inst;
                next_mem.waiting = false;
                next_mem.request_queued = false;
                next_mem.miss = false;
                next_mem.load_value = 0;
                next_mem.alu_result = 0;
                if (inst->op == OP_LW || inst->op == OP_SW) {
                    uint32_t addr = (uint32_t)(c->exec.rs_val + c->exec.rt_val);
                    next_mem.mem_addr = addr & ((1 << 20) - 1);
                    next_mem.store_data = c->exec.rd_val;
                    next_mem.is_load = (inst->op == OP_LW);
                    next_mem.is_store = (inst->op == OP_SW);
                } else {
                    next_mem.is_load = next_mem.is_store = false;
                    next_mem.alu_result = perform_alu(inst, c->exec.rs_val, c->exec.rt_val);
                }
            }

            // DECODE stage: hazard detection (no forwarding) + branch resolution
            bool decode_has_inst = c->decode.valid;
            bool decode_stall = false;
            if (decode_has_inst) {
                c->regs[1] = c->decode.inst.imm;
                int srcs[3];
                int src_count = 0;
                source_regs(&c->decode.inst, srcs, &src_count);
                for (int s = 0; s < src_count; s++) {
                    int reg = srcs[s];
                    if (reg <= 1)
                        continue;
                    // No forwarding: any in-flight writer to the same reg forces a stall
                    if (c->exec.valid && dest_reg(&c->exec.inst) == reg)
                        decode_stall = true;
                    if (c->mem.valid && dest_reg(&c->mem.inst) == reg)
                        decode_stall = true;
                    if (c->wb.valid && dest_reg(&c->wb.inst) == reg)
                        decode_stall = true;
                }
                if (!exec_free_next)
                    decode_stall = true;
                if (decode_stall)
                    c->stats.decode_stall++;
            }

            bool decode_moves = decode_has_inst && !decode_stall && exec_free_next;
            bool decode_free_next = (!c->decode.valid) || decode_moves;
            bool fetch_moves = c->fetch.valid && decode_free_next;

            if (decode_moves) {
                Instruction *inst = &c->decode.inst;
                next_exec.valid = true;
                next_exec.inst = *inst;
                next_exec.rs_val = c->regs[inst->rs];
                next_exec.rt_val = c->regs[inst->rt];
                next_exec.rd_val = c->regs[inst->rd];

                // Branch/jump resolve in decode; delay slot is the following instruction already in fetch
                if (inst->op >= OP_BEQ && inst->op <= OP_BGE) {
                    bool taken = perform_compare(inst, next_exec.rs_val, next_exec.rt_val);
                    if (debug_branch && c->id == 3) {
                        fprintf(stderr, "cycle %d core%d branch pc %03X rs=%08X rt=%08X taken=%d target=%03X\n",
                                cycle, c->id, inst->pc & 0x3FF, (uint32_t)next_exec.rs_val, (uint32_t)next_exec.rt_val,
                                taken, c->regs[inst->rd] & 0x3FF);
                    }
                    if (taken) {
                        c->redirect_pending = true;
                        c->redirect_pc = c->regs[inst->rd] & 0x3FF;
                    }
                } else if (inst->op == OP_JAL) {
                    c->redirect_pending = true;
                    c->redirect_pc = c->regs[inst->rd] & 0x3FF;
                }

                // R1 always mirrors the current instruction immediate (decoded in this cycle)
                c->regs[1] = inst->imm;
                next_decode.valid = false;
            } else if (!decode_stall) {
                next_decode.valid = false;
            }

            if (fetch_moves) {
                next_decode.valid = true;
                next_decode.inst = c->fetch.inst;
            }

            // FETCH stage: pull next instruction unless halted or decode is blocked
            if (!c->stop_fetch && decode_free_next) {
                if (c->redirect_pending) {
                    // branch/jump taken: fetch target while delay slot advances
                    Instruction inst = decode_inst(c->imem[c->redirect_pc], c->redirect_pc);
                    next_fetch.valid = true;
                    next_fetch.inst = inst;
                    c->pc = (c->redirect_pc + 1) & (IMEM_SIZE - 1);
                    c->redirect_pending = false;
                } else {
                    Instruction inst = decode_inst(c->imem[c->pc], c->pc);
                    next_fetch.valid = true;
                    next_fetch.inst = inst;
                    if (inst.op == OP_HALT)
                        c->stop_fetch = true;
                    c->pc = (c->pc + 1) & (IMEM_SIZE - 1);
                }
            } else if (fetch_moves) {
                next_fetch.valid = false;
            }

            c->wb = next_wb;
            c->mem = next_mem;
            c->exec = next_exec;
            c->decode = next_decode;
            c->fetch = next_fetch;

            bool any_valid = c->fetch.valid || c->decode.valid || c->exec.valid || c->mem.valid || c->wb.valid;
            if (c->halted && !any_valid)
                c->done = true;
        }

        // start bus transaction if idle
        if (bus.phase == 0) {
            int chosen = -1;
            for (int k = 0; k < NUM_CORES; k++) {
                int idx = (rr_next + k) % NUM_CORES;
                if (requests[idx].active) {
                    chosen = idx;
                    break;
                }
            }
            if (chosen != -1) {
                rr_next = (chosen + 1) % NUM_CORES;
                BusRequest req = requests[chosen];
                requests[chosen].active = false;
                // Round-robin winner starts transaction; others will retry next cycle
                start_bus_transaction(&bus, &req, cores, main_mem);
            }
        }

        // determine bus output for this cycle (flush beats waiting)
        if (bus.phase == 2) {
            bus.bus_cmd_out = BUS_FLUSH;
            bus.bus_origid_out = bus.provider;
            bus.bus_addr_out = (bus.addr & ~(BLOCK_WORDS - 1)) + bus.index;
            bus.bus_data_out = bus.block[bus.index];
            bus.bus_shared_out = bus.shared;
        } else if (bus.phase == 1 && bus.delay == 0 && bus.bus_cmd_out == BUS_NONE) {
            bus.phase = 2;
            bus.index = 0;
            bus.bus_cmd_out = BUS_FLUSH;
            bus.bus_origid_out = bus.provider;
            bus.bus_addr_out = (bus.addr & ~(BLOCK_WORDS - 1)) + bus.index;
            bus.bus_data_out = bus.block[bus.index];
            bus.bus_shared_out = bus.shared;
        }

        write_bus_trace(bus_fp, cycle, &bus);

        // advance bus state (latency countdown or streaming flush)
        if (bus.phase == 1 && bus.delay > 0) {
            bus.delay--;
        } else if (bus.phase == 2 && bus.bus_cmd_out == BUS_FLUSH) {
            bus.index++;
            if (bus.index >= BLOCK_WORDS) {
                complete_transaction(&bus, cores, main_mem);
                bus.phase = 0;
                bus.cmd = BUS_NONE;
            }
        }

        if (max_cycles >= 0 && cycle >= max_cycles) {
            break;
        }

        bool all_done = true;
        for (int i = 0; i < NUM_CORES; i++) {
            if (!cores[i].done)
                all_done = false;
        }
        if (all_done && bus.phase == 0)
            break;

        cycle++;
    }

    // Close trace files
    for (int i = 0; i < NUM_CORES; i++) {
        if (cores[i].trace_fp)
            fclose(cores[i].trace_fp);
    }
    if (bus_fp)
        fclose(bus_fp);

    // outputs
    write_trimmed_mem(files[5], main_mem, MAIN_MEM_WORDS);
    for (int i = 0; i < NUM_CORES; i++) {
        write_regout(files[6 + i], cores[i].regs);
    }
    for (int i = 0; i < NUM_CORES; i++) {
        write_full_mem(files[15 + i], cores[i].cache.data, CACHE_WORDS);
        // tsram: MESI in 13:12, tag in 11:0
        uint32_t tsram[CACHE_LINES];
        for (int j = 0; j < CACHE_LINES; j++) {
            tsram[j] = ((uint32_t)cores[i].cache.state[j] << 12) | (cores[i].cache.tag[j] & 0xFFF);
        }
        write_full_mem(files[19 + i], tsram, CACHE_LINES);
    }
    for (int i = 0; i < NUM_CORES; i++) {
        write_stats(files[23 + i], &cores[i].stats);
    }
}

int main(int argc, char **argv) {
    const char *defaults[27] = {
        "imem0.txt", "imem1.txt", "imem2.txt", "imem3.txt",
        "memin.txt", "memout.txt",
        "regout0.txt", "regout1.txt", "regout2.txt", "regout3.txt",
        "core0trace.txt", "core1trace.txt", "core2trace.txt", "core3trace.txt",
        "bustrace.txt",
        "dsram0.txt", "dsram1.txt", "dsram2.txt", "dsram3.txt",
        "tsram0.txt", "tsram1.txt", "tsram2.txt", "tsram3.txt",
        "stats0.txt", "stats1.txt", "stats2.txt", "stats3.txt"
    };

    const char *files[27];
    if (argc == 1) {
        for (int i = 0; i < 27; i++)
            files[i] = defaults[i];
    } else if (argc == 28) {
        for (int i = 0; i < 27; i++)
            files[i] = argv[i + 1];
    } else {
        fprintf(stderr, "usage: sim.exe imem0.txt imem1.txt imem2.txt imem3.txt memin.txt memout.txt regout0.txt regout1.txt regout2.txt regout3.txt core0trace.txt core1trace.txt core2trace.txt core3trace.txt bustrace.txt dsram0.txt dsram1.txt dsram2.txt dsram3.txt tsram0.txt tsram1.txt tsram2.txt tsram3.txt stats0.txt stats1.txt stats2.txt stats3.txt\n");
        return 1;
    }

    uint32_t *main_mem = (uint32_t *)calloc(MAIN_MEM_WORDS, sizeof(uint32_t));
    if (!main_mem) {
        fprintf(stderr, "Failed to allocate main memory\n");
        return 1;
    }

    simulate(files, main_mem);

    free(main_mem);
    return 0;
}
