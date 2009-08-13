/* Copyright (c) 2009, Avian Contributors

   Permission to use, copy, modify, and/or distribute this software
   for any purpose with or without fee is hereby granted, provided
   that the above copyright notice and this permission notice appear
   in all copies.

   There is NO WARRANTY for this software.  See license.txt for
   details. */

#include "assembler.h"
#include "vector.h"

#define CAST1(x) reinterpret_cast<UnaryOperationType>(x)
#define CAST2(x) reinterpret_cast<BinaryOperationType>(x)
#define CAST3(x) reinterpret_cast<TernaryOperationType>(x)

using namespace vm;

namespace {

namespace field {
// BITFIELD MASKS
const int64_t MASK_LO32 = 0x0ffffffff;
const int     MASK_LO16 = 0x0ffff;
const int     MASK_LO8  = 0x0ff;
// BITFIELD EXTRACTORS
inline int lo32(int64_t i) { return (int)(i & MASK_LO32); }
inline int hi32(int64_t i) { return lo32(i >> 32); }
inline int lo16(int64_t i) { return (int)(i & MASK_LO16); }
inline int hi16(int64_t i) { return lo16(i >> 16); }
inline int lo8(int64_t i) { return (int)(i & MASK_LO8); }
inline int hi8(int64_t i) { return lo8(i >> 8); }
}

namespace isa {
// INSTRUCTION OPTIONS
enum COND { EQ, NE, CS, CC, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE, AL, NV };
enum SHIFT { LSL, LSR, ASR, ROR };
// INSTRUCTION FORMATS
inline int DATA(int cond, int opcode, int S, int Rn, int Rd, int shift, int Sh, int Rm)
{ return cond<<28 | opcode<<21 | S<<20 | Rn<<16 | Rd<<12 | shift<<7 | Sh<<5 | Rm; }
inline int DATAS(int cond, int opcode, int S, int Rn, int Rd, int Rs, int Sh, int Rm)
{ return cond<<28 | opcode<<21 | S<<20 | Rn<<16 | Rd<<12 | Rs<<8 | Sh<<5 | 1<<4 | Rm; }
inline int DATAI(int cond, int opcode, int S, int Rn, int Rd, int rot, int imm)
{ return cond<<28 | 1<<25 | opcode<<21 | S<<20 | Rn<<16 | Rd<<12 | rot<<8 | imm; }
inline int BRANCH(int cond, int L, int offset)
{ return cond<<28 | 5<<25 | L<<24 | offset; }
inline int BRANCHX(int cond, int L, int Rm)
{ return cond<<28 | 0x4bffc<<6 | L<<5 | 1<<4 | Rm; }
inline int MULTIPLY(int cond, int mul, int S, int Rd, int Rn, int Rs, int Rm)
{ return cond<<28 | mul<<21 | S<<20 | Rd<<16 | Rn<<12 | Rs<<8 | 9<<4 | Rm; }
inline int XFER(int cond, int P, int U, int B, int W, int L, int Rn, int Rd, int shift, int Sh, int Rm)
{ return cond<<28 | 3<<25 | P<<24 | U<<23 | B<<22 | W<<21 | L<<20 | Rn<<16 | Rd<<12 | shift<<7 | Sh<<5 | Rm; }
inline int XFERI(int cond, int P, int U, int B, int W, int L, int Rn, int Rd, int offset)
{ return cond<<28 | 2<<25 | P<<24 | U<<23 | B<<22 | W<<21 | L<<20 | Rn<<16 | Rd<<12 | offset; }
inline int XFER2(int cond, int P, int U, int W, int L, int Rn, int Rd, int S, int H, int Rm)
{ return cond<<28 | P<<24 | U<<23 | W<<21 | L<<20 | Rn<<16 | Rd<<12 | 1<<7 | S<<6 | H<<5 | 1<<4 | Rm; }
inline int XFER2I(int cond, int P, int U, int W, int L, int Rn, int Rd, int offsetH, int S, int H, int offsetL)
{ return cond<<28 | P<<24 | U<<23 | 1<<22 | W<<21 | L<<20 | Rn<<16 | Rd<<12 | offsetH<<8 | 1<<7 | S<<6 | H<<5 | 1<<4 | offsetL; }
inline int BLOCKXFER(int cond, int P, int U, int S, int W, int L, int Rn, int rlist)
{ return cond<<28 | 4<<25 | P<<24 | U<<23 | S<<22 | W<<21 | L<<20 | Rn<<16 | rlist; }
inline int SWI_(int cond, int imm)
{ return cond<<28 | 0x0f<<24 | imm; }
inline int SWAP(int cond, int B, int Rn, int Rd, int Rm)
{ return cond<<28 | 1<<24 | B<<22 | Rn<<16 | Rd<<12 | 9<<4 | Rm; }
// INSTRUCTIONS
// For easier coding, some features (conditions, shifts, etc.) are left out of the instruction compilers.
// They may be enabled using the helper functions included in this namespace.
inline int B(int offset) { return BRANCH(AL, 0, offset); }
inline int BL(int offset) { return BRANCH(AL, 1, offset); }
inline int BX(int Rm) { return BRANCHX(AL, 0, Rm); }
inline int BLX(int Rm) { return BRANCHX(AL, 1, Rm); }
inline int SWI(int imm) { return SWI_(AL, imm); }
inline int AND(int Rd, int Rn, int Rm) { return DATA(AL, 0x0, 0, Rn, Rd, 0, 0, Rm); }
inline int EOR(int Rd, int Rn, int Rm) { return DATA(AL, 0x1, 0, Rn, Rd, 0, 0, Rm); }
inline int SUB(int Rd, int Rn, int Rm) { return DATA(AL, 0x2, 0, Rn, Rd, 0, 0, Rm); }
inline int RSB(int Rd, int Rn, int Rm) { return DATA(AL, 0x3, 0, Rn, Rd, 0, 0, Rm); }
inline int ADD(int Rd, int Rn, int Rm) { return DATA(AL, 0x4, 0, Rn, Rd, 0, 0, Rm); }
inline int ADC(int Rd, int Rn, int Rm) { return DATA(AL, 0x5, 0, Rn, Rd, 0, 0, Rm); }
inline int SBC(int Rd, int Rn, int Rm) { return DATA(AL, 0x6, 0, Rn, Rd, 0, 0, Rm); }
inline int RSC(int Rd, int Rn, int Rm) { return DATA(AL, 0x7, 0, Rn, Rd, 0, 0, Rm); }
inline int TST(int Rn, int Rm) { return DATA(AL, 0x8, 0, Rn, 0, 0, 0, Rm); }
inline int TEQ(int Rn, int Rm) { return DATA(AL, 0x9, 0, Rn, 0, 0, 0, Rm); }
inline int CMP(int Rn, int Rm) { return DATA(AL, 0xa, 0, Rn, 0, 0, 0, Rm); }
inline int CMN(int Rn, int Rm) { return DATA(AL, 0xb, 0, Rn, 0, 0, 0, Rm); }
inline int ORR(int Rd, int Rn, int Rm) { return DATA(AL, 0xc, 0, Rn, Rd, 0, 0, Rm); }
inline int MOV(int Rd, int Rm)         { return DATA(AL, 0xd, 0, 0, Rd, 0, 0, Rm); }
inline int BIC(int Rd, int Rn, int Rm) { return DATA(AL, 0xe, 0, Rn, Rd, 0, 0, Rm); }
inline int MVN(int Rd, int Rm)         { return DATA(AL, 0xf, 0, 0, Rd, 0, 0, Rm); }
inline int ANDi(int Rd, int Rn, int imm) { return DATAI(AL, 0x0, 0, Rn, Rd, 0, imm); }
inline int SUBi(int Rd, int Rn, int imm) { return DATAI(AL, 0x2, 0, Rn, Rd, 0, imm); }
inline int ADDi(int Rd, int Rn, int imm) { return DATAI(AL, 0x4, 0, Rn, Rd, 0, imm); }
inline int ORRi(int Rd, int Rn, int imm) { return DATAI(AL, 0xc, 0, Rn, Rd, 0, imm); }
inline int MOVs(int Rd, int Rm, int Rs, int Sh)         { return DATAS(AL, 0xd, 0, 0, Rd, Rs, Sh, Rm); }
inline int MUL(int Rd, int Rm, int Rs) { return MULTIPLY(AL, 0, 0, Rd, 0, Rs, Rm); }
inline int UMULL(int RdLo, int RdHi, int Rm, int Rs) { return MULTIPLY(AL, 4, 0, RdHi, RdLo, Rs, Rm); } // only avail. on ARM7M series and above
inline int SMULL(int RdLo, int RdHi, int Rm, int Rs) { return MULTIPLY(AL, 6, 0, RdHi, RdLo, Rs, Rm); } // ''
inline int LDR(int Rd, int Rn, int Rm) { return XFER(AL, 1, 1, 0, 0, 1, Rn, Rd, 0, 0, Rm); }
inline int LDRi(int Rd, int Rn, int imm) { return XFERI(AL, 1, 1, 0, 0, 1, Rn, Rd, imm); }
inline int LDRB(int Rd, int Rn, int Rm) { return XFER(AL, 1, 1, 1, 0, 1, Rn, Rd, 0, 0, Rm); }
inline int LDRBi(int Rd, int Rn, int imm) { return XFERI(AL, 1, 1, 1, 0, 1, Rn, Rd, imm); }
inline int STR(int Rd, int Rn, int Rm) { return XFER(AL, 1, 1, 0, 0, 0, Rn, Rd, 0, 0, Rm); }
inline int STRi(int Rd, int Rn, int imm) { return XFERI(AL, 1, 1, 0, 0, 0, Rn, Rd, imm); }
inline int STRB(int Rd, int Rn, int Rm) { return XFER(AL, 1, 1, 1, 0, 0, Rn, Rd, 0, 0, Rm); }
inline int STRBi(int Rd, int Rn, int imm) { return XFERI(AL, 1, 1, 1, 0, 0, Rn, Rd, imm); }
inline int LDRH(int Rd, int Rn, int Rm) { return XFER2(AL, 1, 1, 0, 1, Rn, Rd, 0, 1, Rm); }
inline int LDRHi(int Rd, int Rn, int imm) { return XFER2I(AL, 1, 1, 0, 1, Rn, Rd, imm>>4 & 0xf, 0, 1, imm&0xf); }
inline int STRH(int Rd, int Rn, int Rm) { return XFER2(AL, 1, 1, 0, 0, Rn, Rd, 0, 1, Rm); }
inline int STRHi(int Rd, int Rn, int imm) { return XFER2I(AL, 1, 1, 0, 0, Rn, Rd, imm>>4 & 0xf, 0, 1, imm&0xf); }
inline int LDRSH(int Rd, int Rn, int Rm) { return XFER2(AL, 1, 1, 0, 1, Rn, Rd, 1, 1, Rm); }
inline int LDRSHi(int Rd, int Rn, int imm) { return XFER2I(AL, 1, 1, 0, 1, Rn, Rd, imm>>4 & 0xf, 1, 1, imm&0xf); }
inline int LDRSB(int Rd, int Rn, int Rm) { return XFER2(AL, 1, 1, 0, 1, Rn, Rd, 1, 0, Rm); }
inline int LDRSBi(int Rd, int Rn, int imm) { return XFER2I(AL, 1, 1, 0, 1, Rn, Rd, imm>>4 & 0xf, 1, 0, imm&0xf); }
inline int LDMIB(int Rn, int rlist) { return BLOCKXFER(AL, 1, 1, 0, 0, 1, Rn, rlist); }
inline int LDMIA(int Rn, int rlist) { return BLOCKXFER(AL, 0, 1, 0, 0, 1, Rn, rlist); }
inline int STMIB(int Rn, int rlist) { return BLOCKXFER(AL, 1, 1, 0, 0, 0, Rn, rlist); }
inline int STMDB(int Rn, int rlist) { return BLOCKXFER(AL, 1, 0, 0, 0, 0, Rn, rlist); }
inline int SWP(int Rd, int Rm, int Rn) { return SWAP(AL, 0, Rn, Rd, Rm); }
inline int SWPB(int Rd, int Rm, int Rn) { return SWAP(AL, 1, Rn, Rd, Rm); }
// HELPER FUNCTIONS
inline int setCond(int ins, int cond)  { return ins&0x0fffffff | cond<<28; }
inline int setResult(int ins) { return ins | 1<<20; }
inline int setShift(int ins, int Sh, int shift) { return ins | shift<<7 | Sh<<5; }
inline int setRot(int ins, int rot) { return ins | rot<<8; }
// PSEUDO-INSTRUCTIONS
inline int NOP() { return MOV(0, 0); }
inline int LSL_(int Rd, int Rm, int Rs) { return MOVs(Rd, Rm, Rs, LSL); }
inline int LSLi(int Rd, int Rm, int imm) { return setShift(MOV(Rd, Rm), LSL, imm); }
inline int LSR_(int Rd, int Rm, int Rs) { return MOVs(Rd, Rm, Rs, LSR); }
inline int LSRi(int Rd, int Rm, int imm) { return setShift(MOV(Rd, Rm), LSR, imm); }
inline int ASR_(int Rd, int Rm, int Rs) { return MOVs(Rd, Rm, Rs, ASR); }
inline int ASRi(int Rd, int Rm, int imm) { return setShift(MOV(Rd, Rm), ASR, imm); }
inline int ROR_(int Rd, int Rm, int Rs) { return MOVs(Rd, Rm, Rs, ROR); }
}

inline bool
isInt16(intptr_t v)
{
  return v == static_cast<int16_t>(v);
}

inline int
carry16(intptr_t v)
{
  return static_cast<int16_t>(v) < 0 ? 1 : 0;
}

const unsigned FrameFooterSize = 6;

const int StackRegister = 1;
const int ThreadRegister = 13;

class MyBlock: public Assembler::Block {
 public:
  MyBlock(unsigned offset):
    next(0), offset(offset), start(~0), size(0)
  { }

  virtual unsigned resolve(unsigned start, Assembler::Block* next) {
    this->start = start;
    this->next = static_cast<MyBlock*>(next);

    return start + size;
  }

  MyBlock* next;
  unsigned offset;
  unsigned start;
  unsigned size;
};

class Task;

class Context {
 public:
  Context(System* s, Allocator* a, Zone* zone):
    s(s), zone(zone), client(0), code(s, a, 1024), tasks(0), result(0),
    firstBlock(new (zone->allocate(sizeof(MyBlock))) MyBlock(0)),
    lastBlock(firstBlock)
  { }

  System* s;
  Zone* zone;
  Assembler::Client* client;
  Vector code;
  Task* tasks;
  uint8_t* result;
  MyBlock* firstBlock;
  MyBlock* lastBlock;
};

class Task {
 public:
  Task(Task* next): next(next) { }

  virtual void run(Context* c) = 0;

  Task* next;
};

typedef void (*OperationType)(Context*);

typedef void (*UnaryOperationType)(Context*, unsigned, Assembler::Operand*);

typedef void (*BinaryOperationType)
(Context*, unsigned, Assembler::Operand*, unsigned, Assembler::Operand*);

typedef void (*TernaryOperationType)
(Context*, unsigned, Assembler::Operand*, Assembler::Operand*,
 Assembler::Operand*);

class ArchitectureContext {
 public:
  ArchitectureContext(System* s): s(s) { }

  System* s;
  OperationType operations[OperationCount];
  UnaryOperationType unaryOperations[UnaryOperationCount
                                     * OperandTypeCount];
  BinaryOperationType binaryOperations
  [BinaryOperationCount * OperandTypeCount * OperandTypeCount];
  TernaryOperationType ternaryOperations
  [TernaryOperationCount * OperandTypeCount];
};

inline void NO_RETURN
abort(Context* c)
{
  abort(c->s);
}

inline void NO_RETURN
abort(ArchitectureContext* c)
{
  abort(c->s);
}

#ifndef NDEBUG
inline void
assert(Context* c, bool v)
{
  assert(c->s, v);
}

inline void
assert(ArchitectureContext* c, bool v)
{
  assert(c->s, v);
}
#endif // not NDEBUG

inline void
expect(Context* c, bool v)
{
  expect(c->s, v);
}

class Offset: public Promise {
 public:
  Offset(Context* c, MyBlock* block, unsigned offset):
    c(c), block(block), offset(offset)
  { }

  virtual bool resolved() {
    return block->start != static_cast<unsigned>(~0);
  }
  
  virtual int64_t value() {
    assert(c, resolved());

    return block->start + (offset - block->offset);
  }

  Context* c;
  MyBlock* block;
  unsigned offset;
};

Promise*
offset(Context* c)
{
  return new (c->zone->allocate(sizeof(Offset)))
    Offset(c, c->lastBlock, c->code.length());
}

bool
bounded(int right, int left, int32_t v)
{
  return ((v << left) >> left) == v and ((v >> right) << right) == v;
}

void*
updateOffset(System* s, uint8_t* instruction, bool conditional, int64_t value)
{
  int32_t v = reinterpret_cast<uint8_t*>(value) - instruction;
   
  int32_t mask;
  if (conditional) {
    expect(s, bounded(2, 16, v));
    mask = 0xFFFC;
  } else {
    expect(s, bounded(2, 6, v));
    mask = 0x3FFFFFC;
  }

  int32_t* p = reinterpret_cast<int32_t*>(instruction);
  *p = (v & mask) | ((~mask) & *p);

  return instruction + 4;
}

class OffsetListener: public Promise::Listener {
 public:
  OffsetListener(System* s, uint8_t* instruction, bool conditional):
    s(s),
    instruction(instruction),
    conditional(conditional)
  { }

  virtual bool resolve(int64_t value, void** location) {
    void* p = updateOffset(s, instruction, conditional, value);
    if (location) *location = p;
    return false;
  }

  System* s;
  uint8_t* instruction;
  bool conditional;
};

class OffsetTask: public Task {
 public:
  OffsetTask(Task* next, Promise* promise, Promise* instructionOffset,
             bool conditional):
    Task(next),
    promise(promise),
    instructionOffset(instructionOffset),
    conditional(conditional)
  { }

  virtual void run(Context* c) {
    if (promise->resolved()) {
      updateOffset
        (c->s, c->result + instructionOffset->value(), conditional,
         promise->value());
    } else {
      new (promise->listen(sizeof(OffsetListener)))
        OffsetListener(c->s, c->result + instructionOffset->value(),
                       conditional);
    }
  }

  Promise* promise;
  Promise* instructionOffset;
  bool conditional;
};

void
appendOffsetTask(Context* c, Promise* promise, Promise* instructionOffset,
                 bool conditional)
{
  c->tasks = new (c->zone->allocate(sizeof(OffsetTask))) OffsetTask
    (c->tasks, promise, instructionOffset, conditional);
}

inline unsigned
index(UnaryOperation operation, OperandType operand)
{
  return operation + (UnaryOperationCount * operand);
}

inline unsigned
index(BinaryOperation operation,
      OperandType operand1,
      OperandType operand2)
{
  return operation
    + (BinaryOperationCount * operand1)
    + (BinaryOperationCount * OperandTypeCount * operand2);
}

inline unsigned
index(TernaryOperation operation,
      OperandType operand1)
{
  return operation + (TernaryOperationCount * operand1);
}


// BEGIN OPERATION COMPILERS

using namespace field;

typedef Assembler::Register Reg;
typedef Assembler::Constant Const;

inline void issue(Context* con, int code) { con->code.append4(code); }
inline int getTemp(Context* con) { return con->client->acquireTemporary(); }
inline void freeTemp(Context* con, int r) { con->client->releaseTemporary(r); }
inline int64_t getVal(Const* c) { return c->value->value(); }
inline int R(Reg* r) { return r->low; }
inline int H(Reg* r) { return r->high; }


void shiftLeftR(Context* con, unsigned size, Reg* a, Reg* b, Reg* t)
{
  if(size == 8) {
    Reg Tmp(getTemp(con), getTemp(con)); Reg* tmp = &Tmp;
    issue(con, subfic(H(tmp), R(a), 32));
    issue(con, slw(H(t), H(b), R(a)));
    issue(con, srw(R(tmp), R(b), H(tmp)));
    issue(con, or_(H(t), H(t), R(tmp)));
    issue(con, addi(H(tmp), R(a), -32));
    issue(con, slw(R(tmp), R(b), H(tmp)));
    issue(con, or_(H(t), H(t), R(tmp)));
    freeTemp(con, H(tmp)); freeTemp(con, R(tmp));
  }
  issue(con, slw(R(t), R(b), R(a)));
}

void shiftLeftC(Context* con, unsigned size, Const* a, Reg* b, Reg* t)
{
  int sh = getVal(a);
  if (size == 8) {
    if (sh < 32) {
      issue(con, rlwinm(H(t),H(b),sh,0,31-sh));
      issue(con, rlwimi(H(t),R(b),sh,32-sh,31));
    } else {
      issue(con, rlwinm(H(t),R(b),sh-32,0,63-sh));
      issue(con, li(R(t),0));
    }
  }
  issue(con, slwi(R(t), R(b), sh));
}

void shiftRightR(Context* con, unsigned size, Reg* a, Reg* b, Reg* t)
{
  if(size == 8) {
    Reg Tmp(getTemp(con), getTemp(con)); Reg* tmp = &Tmp;
    issue(con, subfic(H(tmp), R(a), 32));
    issue(con, srw(R(t), R(b), R(a)));
    issue(con, slw(R(tmp), H(b), H(tmp)));
    issue(con, or_(R(t), R(t), R(tmp)));
    issue(con, addic(H(tmp), R(a), -32));
    issue(con, sraw(R(tmp), H(b), H(tmp)));
    issue(con, ble(8));
    issue(con, ori(R(t), R(tmp), 0));
    issue(con, sraw(H(t), H(b), R(a)));
    freeTemp(con, H(tmp)); freeTemp(con, R(tmp));
  } else {
    issue(con, sraw(R(t), R(b), R(a)));
  }
}

void shiftRightC(Context* con, unsigned size, Const* a, Reg* b, Reg* t)
{
  int sh = getVal(a);
  if(size == 8) {
    if (sh < 32) {
      issue(con, rlwinm(R(t),R(b),32-sh,sh,31));
      issue(con, rlwimi(R(t),H(b),32-sh,0,sh-1));
      issue(con, srawi(H(t),H(b),sh));
    } else {
      issue(con, srawi(H(t),H(b),31));
      issue(con, srawi(R(t),H(b),sh-32));
    }
  } else {
    issue(con, srawi(R(t), R(b), sh));
  }
}

void unsignedShiftRightR(Context* con, unsigned size, Reg* a, Reg* b, Reg* t)
{
  issue(con, srw(R(t), R(b), R(a)));
  if(size == 8) {
    Reg Tmp(getTemp(con), getTemp(con)); Reg* tmp = &Tmp;
    issue(con, subfic(H(tmp), R(a), 32));
    issue(con, slw(R(tmp), H(b), H(tmp)));
    issue(con, or_(R(t), R(t), R(tmp)));
    issue(con, addi(H(tmp), R(a), -32));
    issue(con, srw(R(tmp), H(b), H(tmp)));
    issue(con, or_(R(t), R(t), R(tmp)));
    issue(con, srw(H(t), H(b), R(a)));
    freeTemp(con, H(tmp)); freeTemp(con, R(tmp));
  }
}

void unsignedShiftRightC(Context* con, unsigned size, Const* a, Reg* b, Reg* t)
{
  int sh = getVal(a);
  if (size == 8) {
    if (sh < 32) {
      issue(con, srwi(R(t), R(b), sh));
      issue(con, rlwimi(R(t),H(b),32-sh,0,sh-1));
      issue(con, rlwinm(H(t),H(b),32-sh,sh,31));
    } else {
      issue(con, rlwinm(R(t),H(b),64-sh,sh-32,31));
      issue(con, li(H(t),0));
    }
  } else {
    issue(con, srwi(R(t), R(b), sh));
  }
}

void
updateImmediate(System* s, void* dst, int64_t src, unsigned size)
{
  switch (size) {
  case 4: {
    int32_t* p = static_cast<int32_t*>(dst);
    int r = (p[1] >> 21) & 31;

    p[0] = lis(r, src >> 16);
    p[1] = ori(r, r, src);
  } break;

  default: abort(s);
  }
}

class ImmediateListener: public Promise::Listener {
 public:
  ImmediateListener(System* s, void* dst, unsigned size, unsigned offset):
    s(s), dst(dst), size(size), offset(offset)
  { }

  virtual bool resolve(int64_t value, void** location) {
    updateImmediate(s, dst, value, size);
    if (location) *location = static_cast<uint8_t*>(dst) + offset;
    return false;
  }

  System* s;
  void* dst;
  unsigned size;
  unsigned offset;
};

class ImmediateTask: public Task {
 public:
  ImmediateTask(Task* next, Promise* promise, Promise* offset, unsigned size,
                unsigned promiseOffset):
    Task(next),
    promise(promise),
    offset(offset),
    size(size),
    promiseOffset(promiseOffset)
  { }

  virtual void run(Context* c) {
    if (promise->resolved()) {
      updateImmediate
        (c->s, c->result + offset->value(), promise->value(), size);
    } else {
      new (promise->listen(sizeof(ImmediateListener))) ImmediateListener
        (c->s, c->result + offset->value(), size, promiseOffset);
    }
  }

  Promise* promise;
  Promise* offset;
  unsigned size;
  unsigned promiseOffset;
};

void
appendImmediateTask(Context* c, Promise* promise, Promise* offset,
                    unsigned size, unsigned promiseOffset = 0)
{
  c->tasks = new (c->zone->allocate(sizeof(ImmediateTask))) ImmediateTask
    (c->tasks, promise, offset, size, promiseOffset);
}

void
jumpR(Context* c, unsigned size UNUSED, Assembler::Register* target)
{
  assert(c, size == BytesPerWord);

  issue(c, mtctr(target->low));
  issue(c, bctr());
}

void
moveRR(Context* c, unsigned srcSize, Assembler::Register* src,
       unsigned dstSize, Assembler::Register* dst);

void
swapRR(Context* c, unsigned aSize, Assembler::Register* a,
       unsigned bSize, Assembler::Register* b)
{
  assert(c, aSize == BytesPerWord);
  assert(c, bSize == BytesPerWord);

  Assembler::Register tmp(c->client->acquireTemporary());
  moveRR(c, aSize, a, bSize, &tmp);
  moveRR(c, bSize, b, aSize, a);
  moveRR(c, bSize, &tmp, bSize, b);
  c->client->releaseTemporary(tmp.low);
}

void
moveRR(Context* c, unsigned srcSize, Assembler::Register* src,
       unsigned dstSize, Assembler::Register* dst)
{
  switch (srcSize) {
  case 1:
    issue(c, extsb(dst->low, src->low));
    break;
    
  case 2:
    issue(c, extsh(dst->low, src->low));
    break;
    
  case 4:
  case 8:
    if (srcSize == 4 and dstSize == 8) {
      moveRR(c, 4, src, 4, dst);
      issue(c, srawi(dst->high, src->low, 31));
    } else if (srcSize == 8 and dstSize == 8) {
      Assembler::Register srcHigh(src->high);
      Assembler::Register dstHigh(dst->high);

      if (src->high == dst->low) {
        if (src->low == dst->high) {
          swapRR(c, 4, src, 4, dst);
        } else {
          moveRR(c, 4, &srcHigh, 4, &dstHigh);
          moveRR(c, 4, src, 4, dst);
        }
      } else {
        moveRR(c, 4, src, 4, dst);
        moveRR(c, 4, &srcHigh, 4, &dstHigh);
      }
    } else if (src->low != dst->low) {
      issue(c, mr(dst->low, src->low));
    }
    break;

  default: abort(c);
  }
}

void
moveZRR(Context* c, unsigned srcSize, Assembler::Register* src,
        unsigned, Assembler::Register* dst)
{
  switch (srcSize) {
  case 2:
    issue(c, andi(dst->low, src->low, 0xFFFF));
    break;

  default: abort(c);
  }
}

void
moveCR2(Context* c, unsigned, Assembler::Constant* src,
       unsigned dstSize, Assembler::Register* dst, unsigned promiseOffset)
{
  if (dstSize <= 4) {
    if (src->value->resolved()) {
      int32_t v = src->value->value();
      if (isInt16(v)) {
        issue(c, li(dst->low, v));
      } else {
        issue(c, lis(dst->low, v >> 16));
        issue(c, ori(dst->low, dst->low, v));
      }
    } else {
      appendImmediateTask
        (c, src->value, offset(c), BytesPerWord, promiseOffset);
      issue(c, lis(dst->low, 0));
      issue(c, ori(dst->low, dst->low, 0));
    }
  } else {
    abort(c); // todo
  }
}

void
moveCR(Context* c, unsigned srcSize, Assembler::Constant* src,
       unsigned dstSize, Assembler::Register* dst)
{
  moveCR2(c, srcSize, src, dstSize, dst, 0);
}

void addR(Context* con, unsigned size, Reg* a, Reg* b, Reg* t) {
  if(size == 8) {
    issue(con, addc(R(t), R(a), R(b)));
    issue(con, adde(H(t), H(a), H(b)));
  } else {
    issue(con, add(R(t), R(a), R(b)));
  }
}

void addC(Context* con, unsigned size, Const* a, Reg* b, Reg* t) {
  assert(con, size == BytesPerWord);

  int32_t i = getVal(a);
  if(i) {
    issue(con, addi(R(t), R(b), lo16(i)));
    if(not isInt16(i))
      issue(con, addis(R(t), R(t), hi16(i) + carry16(i)));
  } else {
    moveRR(con, size, b, size, t);
  }
}

void subR(Context* con, unsigned size, Reg* a, Reg* b, Reg* t) {
  if(size == 8) {
    issue(con, subfc(R(t), R(a), R(b)));
    issue(con, subfe(H(t), H(a), H(b)));
  } else {
    issue(con, subf(R(t), R(a), R(b)));
  }
}

void subC(Context* c, unsigned size, Const* a, Reg* b, Reg* t) {
  assert(c, size == BytesPerWord);

  ResolvedPromise promise(- a->value->value());
  Assembler::Constant constant(&promise);
  addC(c, size, &constant, b, t);
}

void multiplyR(Context* con, unsigned size, Reg* a, Reg* b, Reg* t) {
  if(size == 8) {
    bool useTemporaries = b->low == t->low;
    int tmpLow;
    int tmpHigh;
    if (useTemporaries) {
      tmpLow = con->client->acquireTemporary();
      tmpHigh = con->client->acquireTemporary();
    } else {
      tmpLow = t->low;
      tmpHigh = t->high;
    }

    issue(con, mullw(tmpHigh, H(a), R(b)));
    issue(con, mullw(tmpLow, R(a), H(b)));
    issue(con, add(H(t), tmpHigh, tmpLow));
    issue(con, mulhwu(tmpLow, R(a), R(b)));
    issue(con, add(H(t), H(t), tmpLow));
    issue(con, mullw(R(t), R(a), R(b)));

    if (useTemporaries) {
      con->client->releaseTemporary(tmpLow);
      con->client->releaseTemporary(tmpHigh);
    }
  } else {
    issue(con, mullw(R(t), R(a), R(b)));
  }
}

void divideR(Context* con, unsigned size UNUSED, Reg* a, Reg* b, Reg* t) {
  assert(con, size == 4);
  issue(con, divw(R(t), R(b), R(a)));
}

void remainderR(Context* con, unsigned size, Reg* a, Reg* b, Reg* t) {
  bool useTemporary = b->low == t->low;
  Assembler::Register tmp(t->low);
  if (useTemporary) {
    tmp.low = con->client->acquireTemporary();
  }

  divideR(con, size, a, b, &tmp);
  multiplyR(con, size, a, &tmp, &tmp);
  subR(con, size, &tmp, b, t);

  if (useTemporary) {
    con->client->releaseTemporary(tmp.low);
  }
}

int
normalize(Context* c, int offset, int index, unsigned scale, 
          bool* preserveIndex, bool* release)
{
  if (offset != 0 or scale != 1) {
    Assembler::Register normalizedIndex
      (*preserveIndex ? c->client->acquireTemporary() : index);
    
    if (*preserveIndex) {
      *release = true;
      *preserveIndex = false;
    } else {
      *release = false;
    }

    int scaled;

    if (scale != 1) {
      Assembler::Register unscaledIndex(index);

      ResolvedPromise scalePromise(log(scale));
      Assembler::Constant scaleConstant(&scalePromise);
      
      shiftLeftC(c, BytesPerWord, &scaleConstant,
                 &unscaledIndex, &normalizedIndex);

      scaled = normalizedIndex.low;
    } else {
      scaled = index;
    }

    if (offset != 0) {
      Assembler::Register untranslatedIndex(scaled);

      ResolvedPromise offsetPromise(offset);
      Assembler::Constant offsetConstant(&offsetPromise);

      addC(c, BytesPerWord, &offsetConstant,
           &untranslatedIndex, &normalizedIndex);
    }

    return normalizedIndex.low;
  } else {
    *release = false;
    return index;
  }
}

void
store(Context* c, unsigned size, Assembler::Register* src,
      int base, int offset, int index, unsigned scale, bool preserveIndex)
{
  if (index != NoRegister) {
    bool release;
    int normalized = normalize
      (c, offset, index, scale, &preserveIndex, &release);

    switch (size) {
    case 1:
      issue(c, stbx(src->low, base, normalized));
      break;

    case 2:
      issue(c, sthx(src->low, base, normalized));
      break;

    case 4:
      issue(c, stwx(src->low, base, normalized));
      break;

    case 8: {
      Assembler::Register srcHigh(src->high);
      store(c, 4, &srcHigh, base, 0, normalized, 1, preserveIndex);
      store(c, 4, src, base, 4, normalized, 1, preserveIndex);
    } break;

    default: abort(c);
    }

    if (release) c->client->releaseTemporary(normalized);
  } else {
    switch (size) {
    case 1:
      issue(c, stb(src->low, base, offset));
      break;

    case 2:
      issue(c, sth(src->low, base, offset));
      break;

    case 4:
      issue(c, stw(src->low, base, offset));
      break;

    case 8: {
      Assembler::Register srcHigh(src->high);
      store(c, 4, &srcHigh, base, offset, NoRegister, 1, false);
      store(c, 4, src, base, offset + 4, NoRegister, 1, false);
    } break;

    default: abort(c);
    }
  }
}

void
moveRM(Context* c, unsigned srcSize, Assembler::Register* src,
       unsigned dstSize UNUSED, Assembler::Memory* dst)
{
  assert(c, srcSize == dstSize);

  store(c, srcSize, src, dst->base, dst->offset, dst->index, dst->scale, true);
}

void
moveAndUpdateRM(Context* c, unsigned srcSize UNUSED, Assembler::Register* src,
                unsigned dstSize UNUSED, Assembler::Memory* dst)
{
  assert(c, srcSize == BytesPerWord);
  assert(c, dstSize == BytesPerWord);
  assert(c, dst->index == NoRegister);

  issue(c, stwu(src->low, dst->base, dst->offset));
}

void
load(Context* c, unsigned srcSize, int base, int offset, int index,
     unsigned scale, unsigned dstSize, Assembler::Register* dst,
     bool preserveIndex, bool signExtend)
{
  if (index != NoRegister) {
    bool release;
    int normalized = normalize
      (c, offset, index, scale, &preserveIndex, &release);

    switch (srcSize) {
    case 1:
      issue(c, lbzx(dst->low, base, normalized));
      if (signExtend) {
        issue(c, extsb(dst->low, dst->low));
      }
      break;

    case 2:
      if (signExtend) {
        issue(c, lhax(dst->low, base, normalized));
      } else {
        issue(c, lhzx(dst->low, base, normalized));
      }
      break;

    case 4:
    case 8: {
      if (srcSize == 4 and dstSize == 8) {
        load(c, 4, base, 0, normalized, 1, 4, dst, preserveIndex, false);
        moveRR(c, 4, dst, 8, dst);
      } else if (srcSize == 8 and dstSize == 8) {
        Assembler::Register dstHigh(dst->high);
        load(c, 4, base, 0, normalized, 1, 4, &dstHigh, preserveIndex, false);
        load(c, 4, base, 4, normalized, 1, 4, dst, preserveIndex, false);
      } else {
        issue(c, lwzx(dst->low, base, normalized));
      }
    } break;

    default: abort(c);
    }

    if (release) c->client->releaseTemporary(normalized);
  } else {
    switch (srcSize) {
    case 1:
      issue(c, lbz(dst->low, base, offset));
      if (signExtend) {
        issue(c, extsb(dst->low, dst->low));
      }
      break;

    case 2:
      if (signExtend) {
        issue(c, lha(dst->low, base, offset));
      } else {
        issue(c, lha(dst->low, base, offset));
      }
      break;

    case 4:
      issue(c, lwz(dst->low, base, offset));
      break;

    case 8: {
      if (srcSize == 4 and dstSize == 8) {
        load(c, 4, base, offset, NoRegister, 1, 4, dst, false, false);
        moveRR(c, 4, dst, 8, dst);
      } else if (srcSize == 8 and dstSize == 8) {
        Assembler::Register dstHigh(dst->high);
        load(c, 4, base, offset, NoRegister, 1, 4, &dstHigh, false, false);
        load(c, 4, base, offset + 4, NoRegister, 1, 4, dst, false, false);
      } else {
        issue(c, lwzx(dst->low, base, offset));
      }
    } break;

    default: abort(c);
    }
  }
}

void
moveMR(Context* c, unsigned srcSize, Assembler::Memory* src,
       unsigned dstSize, Assembler::Register* dst)
{
  load(c, srcSize, src->base, src->offset, src->index, src->scale,
       dstSize, dst, true, true);
}

void
moveZMR(Context* c, unsigned srcSize, Assembler::Memory* src,
        unsigned dstSize, Assembler::Register* dst)
{
  load(c, srcSize, src->base, src->offset, src->index, src->scale,
       dstSize, dst, true, false);
}

// void moveCR3(Context* con, unsigned aSize, Const* a, unsigned tSize, Reg* t) {
//   int64_t i = getVal(a);
//   if(tSize == 8) {
//     int64_t j;
//     if(aSize == 8) j = i; // 64-bit const -> load high bits into high register
//     else           j = 0; // 32-bit const -> clear high register
//     issue(con, lis(H(t), hi16(hi32(j))));
//     issue(con, ori(H(t), H(t), lo16(hi32(j))));
//   }
//   issue(con, lis(R(t), hi16(i)));
//   issue(con, ori(R(t), R(t), lo16(i)));
// }

void
andR(Context* c, unsigned size, Assembler::Register* a,
     Assembler::Register* b, Assembler::Register* dst)
{
  if (size == 8) {
    Assembler::Register ah(a->high);
    Assembler::Register bh(b->high);
    Assembler::Register dh(dst->high);
    
    andR(c, 4, a, b, dst);
    andR(c, 4, &ah, &bh, &dh);
  } else {
    issue(c, and_(dst->low, a->low, b->low));
  }
}

void
andC(Context* c, unsigned size, Assembler::Constant* a,
     Assembler::Register* b, Assembler::Register* dst)
{
  int64_t v = a->value->value();

  if (size == 8) {
    ResolvedPromise high((v >> 32) & 0xFFFFFFFF);
    Assembler::Constant ah(&high);

    ResolvedPromise low(v & 0xFFFFFFFF);
    Assembler::Constant al(&low);

    Assembler::Register bh(b->high);
    Assembler::Register dh(dst->high);

    andC(c, 4, &al, b, dst);
    andC(c, 4, &ah, &bh, &dh);
  } else {
    // bitmasks of the form regex 0*1*0* can be handled in a single
    // rlwinm instruction, hence the following:

    uint32_t v32 = static_cast<uint32_t>(v);
    unsigned state = 0;
    unsigned start = 0;
    unsigned end = 31;
    for (unsigned i = 0; i < 32; ++i) {
      unsigned bit = (v32 >> i) & 1;
      switch (state) {
      case 0:
        if (bit) {
          start = i;
          state = 1;
        }
        break;

      case 1:
        if (bit == 0) {
          end = i - 1;
          state = 2;
        }
        break;

      case 2:
        if (bit) {
          // not in 0*1*0* form.  We can only use andi(s) if either
          // the topmost or bottommost 16 bits are zero.

          if ((v32 >> 16) == 0) {
            issue(c, andi(dst->low, b->low, v32));
          } else if ((v32 & 0xFFFF) == 0) {
            issue(c, andis(dst->low, b->low, v32 >> 16));
          } else {
            bool useTemporary = b->low == dst->low;
            Assembler::Register tmp(dst->low);
            if (useTemporary) {
              tmp.low = c->client->acquireTemporary();
            }

            moveCR(c, 4, a, 4, &tmp);
            andR(c, 4, b, &tmp, dst);

            if (useTemporary) {
              c->client->releaseTemporary(tmp.low);
            }
          }
          return;
        }
        break;
      }
    }

    if (state) {
      if (start != 0 or end != 31) {
        issue(c, rlwinm(dst->low, b->low, 0, 31 - end, 31 - start));
      } else {
        moveRR(c, 4, b, 4, dst);
      }
    } else {
      issue(c, li(dst->low, 0));
    }
  }
}

void
orR(Context* c, unsigned size, Assembler::Register* a,
    Assembler::Register* b, Assembler::Register* dst)
{
  if (size == 8) {
    Assembler::Register ah(a->high);
    Assembler::Register bh(b->high);
    Assembler::Register dh(dst->high);
    
    orR(c, 4, a, b, dst);
    orR(c, 4, &ah, &bh, &dh);
  } else {
    issue(c, or_(dst->low, a->low, b->low));
  }
}

void
orC(Context* c, unsigned size, Assembler::Constant* a,
    Assembler::Register* b, Assembler::Register* dst)
{
  int64_t v = a->value->value();

  if (size == 8) {
    ResolvedPromise high((v >> 32) & 0xFFFFFFFF);
    Assembler::Constant ah(&high);

    ResolvedPromise low(v & 0xFFFFFFFF);
    Assembler::Constant al(&low);

    Assembler::Register bh(b->high);
    Assembler::Register dh(dst->high);

    orC(c, 4, &al, b, dst);
    orC(c, 4, &ah, &bh, &dh);
  } else {
    issue(c, ori(b->low, dst->low, v));
    if (v >> 16) {
      issue(c, oris(dst->low, dst->low, v >> 16));
    }
  }
}

void
xorR(Context* c, unsigned size, Assembler::Register* a,
     Assembler::Register* b, Assembler::Register* dst)
{
  if (size == 8) {
    Assembler::Register ah(a->high);
    Assembler::Register bh(b->high);
    Assembler::Register dh(dst->high);
    
    xorR(c, 4, a, b, dst);
    xorR(c, 4, &ah, &bh, &dh);
  } else {
    issue(c, xor_(dst->low, a->low, b->low));
  }
}

void
xorC(Context* c, unsigned size, Assembler::Constant* a,
     Assembler::Register* b, Assembler::Register* dst)
{
  uint64_t v = a->value->value();

  if (size == 8) {
    ResolvedPromise high((v >> 32) & 0xFFFFFFFF);
    Assembler::Constant ah(&high);

    ResolvedPromise low(v & 0xFFFFFFFF);
    Assembler::Constant al(&low);

    Assembler::Register bh(b->high);
    Assembler::Register dh(dst->high);

    xorC(c, 4, &al, b, dst);
    xorC(c, 4, &ah, &bh, &dh);
  } else {
    if (v >> 16) {
      issue(c, xoris(b->low, dst->low, v >> 16));
      issue(c, xori(dst->low, dst->low, v));
    } else {
      issue(c, xori(b->low, dst->low, v));
    }
  }
}

void
moveAR(Context* c, unsigned srcSize, Assembler::Address* src,
       unsigned dstSize, Assembler::Register* dst)
{
  assert(c, srcSize == 4 and dstSize == 4);

  Assembler::Constant constant(src->address);
  Assembler::Memory memory(dst->low, 0, -1, 0);

  moveCR(c, srcSize, &constant, dstSize, dst);
  moveMR(c, dstSize, &memory, dstSize, dst);
}

void
compareRR(Context* c, unsigned aSize UNUSED, Assembler::Register* a,
          unsigned bSize UNUSED, Assembler::Register* b)
{
  assert(c, aSize == 4 and bSize == 4);
  
  issue(c, cmpw(b->low, a->low));
}

void
compareCR(Context* c, unsigned aSize, Assembler::Constant* a,
          unsigned bSize, Assembler::Register* b)
{
  assert(c, aSize == 4 and bSize == 4);

  if (a->value->resolved() and isInt16(a->value->value())) {
    issue(c, cmpwi(b->low, a->value->value()));
  } else {
    Assembler::Register tmp(c->client->acquireTemporary());
    moveCR(c, aSize, a, bSize, &tmp);
    compareRR(c, bSize, &tmp, bSize, b);
    c->client->releaseTemporary(tmp.low);
  }
}

void
compareCM(Context* c, unsigned aSize, Assembler::Constant* a,
          unsigned bSize, Assembler::Memory* b)
{
  assert(c, aSize == 4 and bSize == 4);

  Assembler::Register tmp(c->client->acquireTemporary());
  moveMR(c, bSize, b, bSize, &tmp);
  compareCR(c, aSize, a, bSize, &tmp);
  c->client->releaseTemporary(tmp.low);
}

void
compareRM(Context* c, unsigned aSize, Assembler::Register* a,
          unsigned bSize, Assembler::Memory* b)
{
  assert(c, aSize == 4 and bSize == 4);

  Assembler::Register tmp(c->client->acquireTemporary());
  moveMR(c, bSize, b, bSize, &tmp);
  compareRR(c, aSize, a, bSize, &tmp);
  c->client->releaseTemporary(tmp.low);
}

void
compareUnsignedRR(Context* c, unsigned aSize UNUSED, Assembler::Register* a,
                  unsigned bSize UNUSED, Assembler::Register* b)
{
  assert(c, aSize == 4 and bSize == 4);
  
  issue(c, cmplw(b->low, a->low));
}

void
compareUnsignedCR(Context* c, unsigned aSize, Assembler::Constant* a,
                  unsigned bSize, Assembler::Register* b)
{
  assert(c, aSize == 4 and bSize == 4);

  if (a->value->resolved() and (a->value->value() >> 16) == 0) {
    issue(c, cmplwi(b->low, a->value->value()));
  } else {
    Assembler::Register tmp(c->client->acquireTemporary());
    moveCR(c, aSize, a, bSize, &tmp);
    compareUnsignedRR(c, bSize, &tmp, bSize, b);
    c->client->releaseTemporary(tmp.low);
  }
}

void
longCompare(Context* c, Assembler::Operand* al, Assembler::Operand* ah,
            Assembler::Operand* bl, Assembler::Operand* bh,
            Assembler::Register* dst, BinaryOperationType compareSigned,
            BinaryOperationType compareUnsigned)
{
  ResolvedPromise negativePromise(-1);
  Assembler::Constant negative(&negativePromise);

  ResolvedPromise zeroPromise(0);
  Assembler::Constant zero(&zeroPromise);

  ResolvedPromise positivePromise(1);
  Assembler::Constant positive(&positivePromise);

  compareSigned(c, 4, ah, 4, bh);

  unsigned less = c->code.length();
  issue(c, blt(0));

  unsigned greater = c->code.length();
  issue(c, bgt(0));

  compareUnsigned(c, 4, al, 4, bl);

  unsigned above = c->code.length();
  issue(c, bgt(0));

  unsigned below = c->code.length();
  issue(c, blt(0));

  moveCR(c, 4, &zero, 4, dst);

  unsigned nextFirst = c->code.length();
  issue(c, b(0));

  updateOffset
    (c->s, c->code.data + less, true, reinterpret_cast<intptr_t>
     (c->code.data + c->code.length()));

  updateOffset
    (c->s, c->code.data + below, true, reinterpret_cast<intptr_t>
     (c->code.data + c->code.length()));

  moveCR(c, 4, &negative, 4, dst);

  unsigned nextSecond = c->code.length();
  issue(c, b(0));

  updateOffset
    (c->s, c->code.data + greater, true, reinterpret_cast<intptr_t>
     (c->code.data + c->code.length()));

  updateOffset
    (c->s, c->code.data + above, true, reinterpret_cast<intptr_t>
     (c->code.data + c->code.length()));

  moveCR(c, 4, &positive, 4, dst);

  updateOffset
    (c->s, c->code.data + nextFirst, false, reinterpret_cast<intptr_t>
     (c->code.data + c->code.length()));

  updateOffset
    (c->s, c->code.data + nextSecond, false, reinterpret_cast<intptr_t>
     (c->code.data + c->code.length()));
}

void
longCompareR(Context* c, unsigned size UNUSED, Assembler::Register* a,
             Assembler::Register* b, Assembler::Register* dst)
{
  assert(c, size == 8);
  
  Assembler::Register ah(a->high);
  Assembler::Register bh(b->high);
  
  longCompare(c, a, &ah, b, &bh, dst, CAST2(compareRR),
              CAST2(compareUnsignedRR));
}

void
longCompareC(Context* c, unsigned size UNUSED, Assembler::Constant* a,
             Assembler::Register* b, Assembler::Register* dst)
{
  assert(c, size == 8);

  int64_t v = a->value->value();

  ResolvedPromise low(v & ~static_cast<uintptr_t>(0));
  Assembler::Constant al(&low);
  
  ResolvedPromise high((v >> 32) & ~static_cast<uintptr_t>(0));
  Assembler::Constant ah(&high);
  
  Assembler::Register bh(b->high);
  
  longCompare(c, &al, &ah, b, &bh, dst, CAST2(compareCR),
              CAST2(compareUnsignedCR));
}

ShiftMaskPromise*
shiftMaskPromise(Context* c, Promise* base, unsigned shift, int64_t mask)
{
  return new (c->zone->allocate(sizeof(ShiftMaskPromise)))
    ShiftMaskPromise(base, shift, mask);
}

void
moveCM(Context* c, unsigned srcSize, Assembler::Constant* src,
       unsigned dstSize, Assembler::Memory* dst)
{
  switch (dstSize) {
  case 8: {
    Assembler::Constant srcHigh
      (shiftMaskPromise(c, src->value, 32, 0xFFFFFFFF));
    Assembler::Constant srcLow
      (shiftMaskPromise(c, src->value, 0, 0xFFFFFFFF));
    
    Assembler::Memory dstLow
      (dst->base, dst->offset + 4, dst->index, dst->scale);
    
    moveCM(c, 4, &srcLow, 4, &dstLow);
    moveCM(c, 4, &srcHigh, 4, dst);
  } break;

  default:
    Assembler::Register tmp(c->client->acquireTemporary());
    moveCR(c, srcSize, src, dstSize, &tmp);
    moveRM(c, dstSize, &tmp, dstSize, dst);
    c->client->releaseTemporary(tmp.low);
  }
}

void
negateRR(Context* c, unsigned srcSize, Assembler::Register* src,
         unsigned dstSize UNUSED, Assembler::Register* dst)
{
  assert(c, srcSize == dstSize);

  if (srcSize == 8) {
    Assembler::Register dstHigh(dst->high);

    issue(c, subfic(dst->low, src->low, 0));
    issue(c, subfze(dst->high, src->high));
  } else {
    issue(c, neg(dst->low, src->low));
  }
}

void
callR(Context* c, unsigned size UNUSED, Assembler::Register* target)
{
  assert(c, size == BytesPerWord);

  issue(c, mtctr(target->low));
  issue(c, bctrl());
}

void
callC(Context* c, unsigned size UNUSED, Assembler::Constant* target)
{
  assert(c, size == BytesPerWord);

  appendOffsetTask(c, target->value, offset(c), false);
  issue(c, bl(0));
}

void
longCallC(Context* c, unsigned size UNUSED, Assembler::Constant* target)
{
  assert(c, size == BytesPerWord);

  Assembler::Register tmp(0);
  moveCR2(c, BytesPerWord, target, BytesPerWord, &tmp, 12);
  callR(c, BytesPerWord, &tmp);
}

void
longJumpC(Context* c, unsigned size UNUSED, Assembler::Constant* target)
{
  assert(c, size == BytesPerWord);

  Assembler::Register tmp(0);
  moveCR2(c, BytesPerWord, target, BytesPerWord, &tmp, 12);
  jumpR(c, BytesPerWord, &tmp);
}

void
jumpC(Context* c, unsigned size UNUSED, Assembler::Constant* target)
{
  assert(c, size == BytesPerWord);

  appendOffsetTask(c, target->value, offset(c), false);
  issue(c, b(0));
}

void
jumpIfEqualC(Context* c, unsigned size UNUSED, Assembler::Constant* target)
{
  assert(c, size == BytesPerWord);

  appendOffsetTask(c, target->value, offset(c), true);
  issue(c, be(0));
}

void
jumpIfNotEqualC(Context* c, unsigned size UNUSED, Assembler::Constant* target)
{
  assert(c, size == BytesPerWord);

  appendOffsetTask(c, target->value, offset(c), true);
  issue(c, bne(0));
}

void
jumpIfGreaterC(Context* c, unsigned size UNUSED, Assembler::Constant* target)
{
  assert(c, size == BytesPerWord);

  appendOffsetTask(c, target->value, offset(c), true);
  issue(c, bgt(0));
}

void
jumpIfGreaterOrEqualC(Context* c, unsigned size UNUSED,
                      Assembler::Constant* target)
{
  assert(c, size == BytesPerWord);

  appendOffsetTask(c, target->value, offset(c), true);
  issue(c, bge(0));
}

void
jumpIfLessC(Context* c, unsigned size UNUSED, Assembler::Constant* target)
{
  assert(c, size == BytesPerWord);

  appendOffsetTask(c, target->value, offset(c), true);
  issue(c, blt(0));
}

void
jumpIfLessOrEqualC(Context* c, unsigned size UNUSED,
                   Assembler::Constant* target)
{
  assert(c, size == BytesPerWord);

  appendOffsetTask(c, target->value, offset(c), true);
  issue(c, ble(0));
}

void
return_(Context* c)
{
  issue(c, blr());
}

void
memoryBarrier(Context* c)
{
  issue(c, sync(0));
}

// END OPERATION COMPILERS


void
populateTables(ArchitectureContext* c)
{
  const OperandType C = ConstantOperand;
  const OperandType A = AddressOperand;
  const OperandType R = RegisterOperand;
  const OperandType M = MemoryOperand;

  OperationType* zo = c->operations;
  UnaryOperationType* uo = c->unaryOperations;
  BinaryOperationType* bo = c->binaryOperations;
  TernaryOperationType* to = c->ternaryOperations;

  zo[Return] = return_;
  zo[LoadBarrier] = memoryBarrier;
  zo[StoreStoreBarrier] = memoryBarrier;
  zo[StoreLoadBarrier] = memoryBarrier;

  uo[index(LongCall, C)] = CAST1(longCallC);

  uo[index(LongJump, C)] = CAST1(longJumpC);

  uo[index(Jump, R)] = CAST1(jumpR);
  uo[index(Jump, C)] = CAST1(jumpC);

  uo[index(JumpIfEqual, C)] = CAST1(jumpIfEqualC);
  uo[index(JumpIfNotEqual, C)] = CAST1(jumpIfNotEqualC);
  uo[index(JumpIfGreater, C)] = CAST1(jumpIfGreaterC);
  uo[index(JumpIfGreaterOrEqual, C)] = CAST1(jumpIfGreaterOrEqualC);
  uo[index(JumpIfLess, C)] = CAST1(jumpIfLessC);
  uo[index(JumpIfLessOrEqual, C)] = CAST1(jumpIfLessOrEqualC);

  uo[index(Call, C)] = CAST1(callC);
  uo[index(Call, R)] = CAST1(callR);

  uo[index(AlignedCall, C)] = CAST1(callC);
  uo[index(AlignedCall, R)] = CAST1(callR);

  bo[index(Move, R, R)] = CAST2(moveRR);
  bo[index(Move, C, R)] = CAST2(moveCR);
  bo[index(Move, C, M)] = CAST2(moveCM);
  bo[index(Move, M, R)] = CAST2(moveMR);
  bo[index(Move, R, M)] = CAST2(moveRM);
  bo[index(Move, A, R)] = CAST2(moveAR);

  bo[index(MoveZ, R, R)] = CAST2(moveZRR);
  bo[index(MoveZ, M, R)] = CAST2(moveZMR);
  bo[index(MoveZ, C, R)] = CAST2(moveCR);

  bo[index(Compare, R, R)] = CAST2(compareRR);
  bo[index(Compare, C, R)] = CAST2(compareCR);
  bo[index(Compare, R, M)] = CAST2(compareRM);
  bo[index(Compare, C, M)] = CAST2(compareCM);

  bo[index(Negate, R, R)] = CAST2(negateRR);

  to[index(Add, R)] = CAST3(addR);
  to[index(Add, C)] = CAST3(addC);

  to[index(Subtract, R)] = CAST3(subR);
  to[index(Subtract, C)] = CAST3(subC);

  to[index(Multiply, R)] = CAST3(multiplyR);

  to[index(Divide, R)] = CAST3(divideR);

  to[index(Remainder, R)] = CAST3(remainderR);

  to[index(ShiftLeft, R)] = CAST3(shiftLeftR);
  to[index(ShiftLeft, C)] = CAST3(shiftLeftC);

  to[index(ShiftRight, R)] = CAST3(shiftRightR);
  to[index(ShiftRight, C)] = CAST3(shiftRightC);

  to[index(UnsignedShiftRight, R)] = CAST3(unsignedShiftRightR);
  to[index(UnsignedShiftRight, C)] = CAST3(unsignedShiftRightC);

  to[index(And, C)] = CAST3(andC);
  to[index(And, R)] = CAST3(andR);

  to[index(Or, C)] = CAST3(orC);
  to[index(Or, R)] = CAST3(orR);

  to[index(Xor, C)] = CAST3(xorC);
  to[index(Xor, R)] = CAST3(xorR);

  to[index(LongCompare, R)] = CAST3(longCompareR);
  to[index(LongCompare, C)] = CAST3(longCompareC);
}

class MyArchitecture: public Assembler::Architecture {
 public:
  MyArchitecture(System* system): c(system), referenceCount(0) {
    populateTables(&c);
  }

  virtual unsigned registerCount() {
    return 32;
  }

  virtual int stack() {
    return StackRegister;
  }

  virtual int thread() {
    return ThreadRegister;
  }

  virtual int returnLow() {
    return 4;
  }

  virtual int returnHigh() {
    return (BytesPerWord == 4 ? 3 : NoRegister);
  }

  virtual bool condensedAddressing() {
    return false;
  }

  virtual bool bigEndian() {
    return true;
  }

  virtual bool reserved(int register_) {
    switch (register_) {
    case 0: // r0 has special meaning in addi and other instructions
    case StackRegister:
    case ThreadRegister:
      return true;

    default:
      return false;
    }
  }

  virtual unsigned argumentFootprint(unsigned footprint) {
    return footprint;
  }

  virtual unsigned argumentRegisterCount() {
    return 8;
  }

  virtual int argumentRegister(unsigned index) {
    assert(&c, index < argumentRegisterCount());

    return index + 3;
  }

  virtual void updateCall(UnaryOperation op UNUSED,
                          bool assertAlignment UNUSED, void* returnAddress,
                          void* newTarget)
  {
    switch (op) {
    case Call:
    case Jump: {
      updateOffset(c.s, static_cast<uint8_t*>(returnAddress) - 4, false,
                   reinterpret_cast<intptr_t>(newTarget));
    } break;

    case LongCall:
    case LongJump: {
      updateImmediate(c.s, static_cast<uint8_t*>(returnAddress) - 12,
                      reinterpret_cast<intptr_t>(newTarget), BytesPerWord);
    } break;

    default: abort(&c);
    }
  }

  virtual uintptr_t getConstant(const void* src) {
    const int32_t* p = static_cast<const int32_t*>(src);
    return (p[0] << 16) | (p[1] & 0xFFFF);    
  }

  virtual void setConstant(void* dst, uintptr_t constant) {
    updateImmediate(c.s, dst, constant, BytesPerWord);
  }

  virtual unsigned alignFrameSize(unsigned sizeInWords) {
    const unsigned alignment = 16 / BytesPerWord;
    return (ceiling(sizeInWords + FrameFooterSize, alignment) * alignment);
  }

  virtual void* frameIp(void* stack) {
    return stack ? static_cast<void**>(stack)[2] : 0;
  }

  virtual unsigned frameHeaderSize() {
    return 0;
  }

  virtual unsigned frameReturnAddressSize() {
    return 0;
  }

  virtual unsigned frameFooterSize() {
    return FrameFooterSize;
  }

  virtual void nextFrame(void** stack, void**) {
    assert(&c, *static_cast<void**>(*stack) != *stack);

    *stack = *static_cast<void**>(*stack);
  }

  virtual void plan
  (UnaryOperation,
   unsigned, uint8_t* aTypeMask, uint64_t* aRegisterMask,
   bool* thunk)
  {
    *aTypeMask = (1 << RegisterOperand) | (1 << ConstantOperand);
    *aRegisterMask = ~static_cast<uint64_t>(0);
    *thunk = false;
  }

  virtual void plan
  (BinaryOperation op,
   unsigned, uint8_t* aTypeMask, uint64_t* aRegisterMask,
   unsigned, uint8_t* bTypeMask, uint64_t* bRegisterMask,
   bool* thunk)
  {
    *aTypeMask = ~0;
    *aRegisterMask = ~static_cast<uint64_t>(0);

    *bTypeMask = (1 << RegisterOperand) | (1 << MemoryOperand);
    *bRegisterMask = ~static_cast<uint64_t>(0);

    *thunk = false;

    switch (op) {
    case Compare:
      *aTypeMask = (1 << RegisterOperand) | (1 << ConstantOperand);
      *bTypeMask = (1 << RegisterOperand);
      break;

    case Negate:
      *aTypeMask = (1 << RegisterOperand);
      *bTypeMask = (1 << RegisterOperand);
      break;

    default:
      break;
    }
  }

  virtual void plan
  (TernaryOperation op,
   unsigned aSize, uint8_t* aTypeMask, uint64_t* aRegisterMask,
   unsigned, uint8_t* bTypeMask, uint64_t* bRegisterMask,
   unsigned, uint8_t* cTypeMask, uint64_t* cRegisterMask,
   bool* thunk)
  {
    *aTypeMask = (1 << RegisterOperand) | (1 << ConstantOperand);
    *aRegisterMask = ~static_cast<uint64_t>(0);

    *bTypeMask = (1 << RegisterOperand);
    *bRegisterMask = ~static_cast<uint64_t>(0);

    *thunk = false;

    switch (op) {
    case Add:
    case Subtract:
      if (aSize == 8) {
        *aTypeMask = *bTypeMask = (1 << RegisterOperand);
      }
      break;

    case Multiply:
      *aTypeMask = *bTypeMask = (1 << RegisterOperand);
      break;

    case LongCompare:
      *bTypeMask = (1 << RegisterOperand);
      break;

    case Divide:
    case Remainder:
      if (BytesPerWord == 4 and aSize == 8) {
        *bTypeMask = ~0;
        *thunk = true;        
      } else {
        *aTypeMask = (1 << RegisterOperand);
      }
      break;

    default:
      break;
    }

    *cTypeMask = *bTypeMask;
    *cRegisterMask = *bRegisterMask;
  }

  virtual void acquire() {
    ++ referenceCount;
  }

  virtual void release() {
    if (-- referenceCount == 0) {
      c.s->free(this);
    }
  }

  ArchitectureContext c;
  unsigned referenceCount;
};

class MyAssembler: public Assembler {
 public:
  MyAssembler(System* s, Allocator* a, Zone* zone, MyArchitecture* arch):
    c(s, a, zone), arch_(arch)
  { }

  virtual void setClient(Client* client) {
    assert(&c, c.client == 0);
    c.client = client;
  }

  virtual Architecture* arch() {
    return arch_;
  }

  virtual void saveFrame(unsigned stackOffset, unsigned) {
    Register stack(StackRegister);
    Memory stackDst(ThreadRegister, stackOffset);
    moveRM(&c, BytesPerWord, &stack, BytesPerWord, &stackDst);
  }

  virtual void pushFrame(unsigned argumentCount, ...) {
    struct {
      unsigned size;
      OperandType type;
      Operand* operand;
    } arguments[argumentCount];

    va_list a; va_start(a, argumentCount);
    unsigned footprint = 0;
    for (unsigned i = 0; i < argumentCount; ++i) {
      arguments[i].size = va_arg(a, unsigned);
      arguments[i].type = static_cast<OperandType>(va_arg(a, int));
      arguments[i].operand = va_arg(a, Operand*);
      footprint += ceiling(arguments[i].size, BytesPerWord);
    }
    va_end(a);

    allocateFrame(arch_->alignFrameSize(footprint));
    
    unsigned offset = 0;
    for (unsigned i = 0; i < argumentCount; ++i) {
      if (i < arch_->argumentRegisterCount()) {
        Register dst(arch_->argumentRegister(i));

        apply(Move,
              arguments[i].size, arguments[i].type, arguments[i].operand,
              pad(arguments[i].size), RegisterOperand, &dst);

        offset += ceiling(arguments[i].size, BytesPerWord);
      } else {
        Memory dst(ThreadRegister, (offset + FrameFooterSize) * BytesPerWord);

        apply(Move,
              arguments[i].size, arguments[i].type, arguments[i].operand,
              pad(arguments[i].size), MemoryOperand, &dst);

        offset += ceiling(arguments[i].size, BytesPerWord);
      }
    }
  }

  virtual void allocateFrame(unsigned footprint) {
    Register returnAddress(0);
    issue(&c, mflr(returnAddress.low));

    Memory returnAddressDst(StackRegister, 8);
    moveRM(&c, BytesPerWord, &returnAddress, BytesPerWord, &returnAddressDst);

    Register stack(StackRegister);
    Memory stackDst(StackRegister, -footprint * BytesPerWord);
    moveAndUpdateRM(&c, BytesPerWord, &stack, BytesPerWord, &stackDst);
  }

  virtual void popFrame() {
    Register stack(StackRegister);
    Memory stackSrc(StackRegister, 0);
    moveMR(&c, BytesPerWord, &stackSrc, BytesPerWord, &stack);

    Assembler::Register returnAddress(0);
    Assembler::Memory returnAddressSrc(StackRegister, 8);
    moveMR(&c, BytesPerWord, &returnAddressSrc, BytesPerWord, &returnAddress);
    
    issue(&c, mtlr(returnAddress.low));
  }

  virtual void apply(Operation op) {
    arch_->c.operations[op](&c);
  }

  virtual void apply(UnaryOperation op,
                     unsigned aSize, OperandType aType, Operand* aOperand)
  {
    arch_->c.unaryOperations[index(op, aType)](&c, aSize, aOperand);
  }

  virtual void apply(BinaryOperation op,
                     unsigned aSize, OperandType aType, Operand* aOperand,
                     unsigned bSize, OperandType bType, Operand* bOperand)
  {
    arch_->c.binaryOperations[index(op, aType, bType)]
      (&c, aSize, aOperand, bSize, bOperand);
  }

  virtual void apply(TernaryOperation op,
                     unsigned, OperandType aType, Operand* aOperand,
                     unsigned bSize, OperandType bType UNUSED,
                     Operand* bOperand,
                     unsigned cSize UNUSED, OperandType cType UNUSED,
                     Operand* cOperand)
  {
    assert(&c, bSize == cSize);
    assert(&c, bType == RegisterOperand);
    assert(&c, cType == RegisterOperand);

    arch_->c.ternaryOperations[index(op, aType)]
      (&c, bSize, aOperand, bOperand, cOperand);
  }

  virtual void writeTo(uint8_t* dst) {
    c.result = dst;
    
    for (MyBlock* b = c.firstBlock; b; b = b->next) {
      memcpy(dst + b->start, c.code.data + b->offset, b->size);
    }
    
    for (Task* t = c.tasks; t; t = t->next) {
      t->run(&c);
    }
  }

  virtual Promise* offset() {
    return ::offset(&c);
  }

  virtual Block* endBlock(bool startNew) {
    MyBlock* b = c.lastBlock;
    b->size = c.code.length() - b->offset;
    if (startNew) {
      c.lastBlock = new (c.zone->allocate(sizeof(MyBlock)))
        MyBlock(c.code.length());
    } else {
      c.lastBlock = 0;
    }
    return b;
  }

  virtual unsigned length() {
    return c.code.length();
  }

  virtual void dispose() {
    c.code.dispose();
  }

  Context c;
  MyArchitecture* arch_;
};

} // namespace

namespace vm {

Assembler::Architecture*
makeArchitecture(System* system)
{
  return new (allocate(system, sizeof(MyArchitecture))) MyArchitecture(system);
}

Assembler*
makeAssembler(System* system, Allocator* allocator, Zone* zone,
              Assembler::Architecture* architecture)
{
  return new (zone->allocate(sizeof(MyAssembler)))
    MyAssembler(system, allocator, zone,
                static_cast<MyArchitecture*>(architecture));
}

} // namespace vm