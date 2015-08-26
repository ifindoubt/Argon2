/*
 * Argon2 source code package
 * 
 * This work is licensed under a Creative Commons CC0 1.0 License/Waiver.
 * 
 * You should have received a copy of the CC0 Public Domain Dedication along with
 * this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
 */


#pragma once

#ifndef __ARGON2_CORE_H__
#define __ARGON2_CORE_H__

#include <cstring> 

/*************************Argon2 internal constants**************************************************/

/* Version of the algorithm */
const uint8_t VERSION_NUMBER = 0x10;

/* Memory block size in bytes */
const uint32_t BLOCK_SIZE = 1024;
const uint32_t WORDS_IN_BLOCK = BLOCK_SIZE / sizeof (uint64_t);
const uint32_t QWORDS_IN_BLOCK = WORDS_IN_BLOCK / 2;

/* Number of pseudo-random values generated by one call to Blake in Argon2i  to generate reference block positions*/
const uint32_t ADDRESSES_IN_BLOCK = (BLOCK_SIZE * sizeof (uint8_t) / sizeof (uint64_t));

/* Pre-hashing digest length and its extension*/
const uint32_t PREHASH_DIGEST_LENGTH = 64;
const uint32_t PREHASH_SEED_LENGTH = PREHASH_DIGEST_LENGTH + 8;

/* Argon2 primitive type */
enum Argon2_type {
    Argon2_d,
    Argon2_i,
    Argon2_di,
    Argon2_id,
    Argon2_ds,
};

/*****SM-related constants******/
const uint32_t SBOX_SIZE = 1 << 10;
const uint32_t SBOX_MASK = SBOX_SIZE / 2 - 1;


/*************************Argon2 internal data types**************************************************/

/*
 * Structure for the (1KB) memory block implemented as 128 64-bit words.
 * Memory blocks can be copied, XORed. Internal words can be accessed by [] (no bounds checking).
 */
struct block {
    uint64_t v[WORDS_IN_BLOCK];

    block(uint8_t in = 0) { //default ctor
        memset(v, in, BLOCK_SIZE);
    }

    uint64_t& operator[](const uint8_t i) { //Subscript operator
        return v[i];
    }

    block& operator=(const block& r) { //Assignment operator
        memcpy(v, r.v, BLOCK_SIZE);
        return *this;
    }

    block& operator^=(const block& r) { //Xor-assignment
        for (uint8_t j = 0; j < WORDS_IN_BLOCK; ++j) {
            v[j] ^= r.v[j];
        }
        return *this;
    }

    block(const block& r) {
        memcpy(v, r.v, BLOCK_SIZE);
    }
};

/*
 *  XORs two blocks
 * @param  l  Left operand
 * @param  r  Right operand
 * @return Xors of the blocks
 */
block operator^(const block& l, const block& r);

/*
 * Argon2 instance: memory pointer, number of passes, amount of memory, type, and derived values. 
 * Used to evaluate the number and location of blocks to construct in each thread
 */
struct Argon2_instance_t {
    block* state; //Memory pointer
    const uint32_t passes; //Number of passes
    const uint32_t memory_blocks; //Number of blocks in memory
    const uint32_t segment_length;
    const uint32_t lane_length;
    const uint8_t lanes;
    const Argon2_type type;
    uint64_t *Sbox; //S-boxes for Argon2_ds

    Argon2_instance_t(block* ptr = NULL, Argon2_type t = Argon2_d, uint32_t p = 1, uint32_t m = 8, uint8_t l = 1) :
    state(ptr), type(t), passes(p), memory_blocks(m), lanes(l), segment_length(m / (l*SYNC_POINTS)), lane_length(m / l), Sbox(NULL) {
    };
};

/*
 * Argon2 position: where we construct the block right now. Used to distribute work between threads.
 */
struct Argon2_position_t {
    const uint32_t pass;
    const uint8_t lane;
    const uint8_t slice;
    uint32_t index;

    Argon2_position_t(uint32_t p = 0, uint8_t l = 0, uint8_t s = 0, uint32_t i = 0) : pass(p), slice(s), lane(l), index(i) {
    };
};

/*Macro for endianness conversion*/

#if defined(_MSC_VER) 
#define BSWAP32(x) _byteswap_ulong(x)
#else
#define BSWAP32(x) __builtin_bswap32(x)
#endif

/*************************Argon2 core functions**************************************************/

/* Allocates memory to the given pointer
 * @param memory pointer to the pointer to the memory
 * @param m_cost number of blocks to allocate in the memory
 * @return ARGON2_OK if @memory is a valid pointer and memory is allocated
 */
int AllocateMemory(block **memory, uint32_t m_cost);

/* Deallocates memory
 * @param instance pointer to the current instance
 * @param clear_memory indicates if we clear the memory with zeros.
 */
void FreeMemory(Argon2_instance_t* instance, bool clear_memory);

/*
 * Generate pseudo-random values to reference blocks in the segment and puts them into the array
 * @param instance Pointer to the current instance
 * @param position Pointer to the current position
 * @param pseudo_rands Pointer to the array of 64-bit values
 * @pre pseudo_rands must point to @a instance->segment_length allocated values
 */
void GenerateAddresses(const Argon2_instance_t* instance, const Argon2_position_t* position, uint64_t* pseudo_rands);

/*
 * Computes absolute position of reference block in the lane following a skewed distribution and using a pseudo-random value as input
 * @param instance Pointer to the current instance
 * @param position Pointer to the current position
 * @param pseudo_rand 32-bit pseudo-random value used to determine the position
 * @param same_lane Indicates if the block will be taken from the current lane. If so we can reference the current segment
 * @pre All pointers must be valid
 */
uint32_t IndexAlpha(const Argon2_instance_t* instance, const Argon2_position_t* position, uint32_t pseudo_rand, bool same_lane);

/*
 * Function that validates all inputs against predefined restrictions and return an error code
 * @param context Pointer to current Argon2 context
 * @return ARGON2_OK if everything is all right, otherwise one of error codes (all defined in <argon2.h>
 */
int ValidateInputs(const Argon2_Context* context);

/**
 *Functions converts number to little endian if needed
 *@param input number to be converted
 *@return number with bytes in reversed order
 */
static inline uint32_t ToLittleEndian(uint32_t input);

/*
 * Hashes all the inputs into @a blockhash[PREHASH_DIGEST_LENGTH], clears password and secret if needed
 * @param  context  Pointer to the Argon2 internal structure containing memory pointer, and parameters for time and space requirements.
 * @param  blockhash Buffer for pre-hashing digest
 * @param  type Argon2 type
 * @pre    @a blockhash must have at least @a PREHASH_DIGEST_LENGTH bytes allocated
 */
void InitialHash(uint8_t* blockhash, const Argon2_Context* context, Argon2_type type);

/*
 * Function creates first 2 blocks per lane
 * @param instance Pointer to the current instance
 * @param blockhash Pointer to the pre-hashing digest
 * @pre blockhash must point to @a PREHASH_SEED_LENGTH allocated values
 */
void FillFirstBlocks(uint8_t* blockhash, const Argon2_instance_t* instance);


/*
 * Function allocates memory, hashes the inputs with Blake,  and creates first two blocks. Returns the pointer to the main memory with 2 blocks per lane
 * initialized
 * @param  context  Pointer to the Argon2 internal structure containing memory pointer, and parameters for time and space requirements.
 * @param  instance Current Argon2 instance
 * @return Zero if successful, -1 if memory failed to allocate. @context->state will be modified if successful.
 */
int Initialize(Argon2_instance_t* instance, Argon2_Context* context);

/*
 * XORing the last block of each lane, hashing it, making the tag. Deallocates the memory.
 * @param context Pointer to current Argon2 context (use only the out parameters from it)
 * @param instance Pointer to current instance of Argon2
 * @pre instance->state must point to necessary amount of memory
 * @pre context->out must point to outlen bytes of memory
 * @pre if context->free_cbk is not NULL, it should point to a function that deallocates memory
 */
void Finalize(const Argon2_Context *context, Argon2_instance_t* instance);


/*
 * Function fills a new memory block
 * @param prev_block Pointer to the previous block
 * @param ref_block Pointer to the reference block
 * @param next_block Pointer to the block to be constructed
 * @param Sbox Pointer to the Sbox (used in Argon2_ds only)
 * @pre all block pointers must be valid
 */
void FillBlock(const block* prev_block, const block* ref_block, block* next_block, const uint64_t* Sbox);

/*
 * Function that fills the segment using previous segments also from other threads
 * @param instance Pointer to the current instance
 * @param position Current position
 * @pre all block pointers must be valid
 */
void FillSegment(const Argon2_instance_t* instance, Argon2_position_t position);

/*
 * Function that fills the entire memory t_cost times based on the first two blocks in each lane
 * @param instance Pointer to the current instance
 */
void FillMemory(const Argon2_instance_t* instance);


/*
 * Function that performs memory-hard hashing with certain degree of parallelism
 * @param  context  Pointer to the Argon2 internal structure
 * @return Error code if smth is wrong, ARGON2_OK otherwise
 */
int Argon2Core(Argon2_Context* context, Argon2_type type);

/*
 * Generates the Sbox from the first memory block (must be ready at that time)
 * @param instance Pointer to the current instance 
 */
void GenerateSbox(Argon2_instance_t* instance);

#endif
