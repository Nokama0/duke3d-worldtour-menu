#pragma once
// The prelude Megaton's ported test blocks compile against: only the pure headers the ports share. CHECK keeps its
// one-argument signature so blocks port unrewritten.
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include "../common.h"
#include "../target.h"
#include "../actorstate.h"
#include "../aimdiag.h"
#include "../firerate.h"

extern int passed, failed;

#define CHECK1(cond) do { if (cond) passed++; else { failed++; \
  printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK2(cond, what) do { if (cond) passed++; else { failed++; \
  printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, what); } } while (0)
#define CHECK_PICK(_1, _2, NAME, ...) NAME
#define CHECK(...) CHECK_PICK(__VA_ARGS__, CHECK2, CHECK1)(__VA_ARGS__)

static inline bool streq(const char* a, const char* b) {
  return a && b && strcmp(a, b) == 0;
}

// The action-query stub Megaton's ported blocks drive: records what it was asked and answers yes or no.
extern const void* q_stub_last_addr;
extern unsigned q_stub_last_bytes;
int q_stub_refuse(const void* addr, unsigned bytes);
int q_stub_accept(const void* addr, unsigned bytes);
