/*
 * Argon2 source code package
 * 
 * Written by Daniel Dinu and Dmitry Khovratovich, 2015
 * 
 * This work is licensed under a Creative Commons CC0 1.0 License/Waiver.
 * 
 * You should have received a copy of the CC0 Public Domain Dedication along with
 * this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
 */


#pragma once

#ifndef __ARGON2_CORE_H__
#define __ARGON2_CORE_H__

#include <inttypes.h>

/*************************Argon2 internal constants**************************************************/

/* Version of the algorithm */
extern const uint32_t ARGON2_VERSION_NUMBER;

/* Memory block size in bytes */
#define  ARGON2_BLOCK_SIZE  1024
#define ARGON2_WORDS_IN_BLOCK (ARGON2_BLOCK_SIZE/8)
extern const uint32_t ARGON2_QWORDS_IN_BLOCK ; /*Dependent values!*/

/* Number of pseudo-random values generated by one call to Blake in Argon2i  to generate reference block positions*/
extern const uint32_t ARGON2_ADDRESSES_IN_BLOCK;

/* Pre-hashing digest length and its extension*/
extern const uint32_t ARGON2_PREHASH_DIGEST_LENGTH ;
extern const uint32_t ARGON2_PREHASH_SEED_LENGTH ;/*Dependent values!*/



/*****SM-related constants******/
extern const uint32_t ARGON2_SBOX_SIZE;
extern const uint32_t ARGON2_SBOX_MASK ;

/* Argon2 primitive type */
typedef enum _Argon2_type {
    Argon2_d=0,
    Argon2_i=1,
    Argon2_id=2,
    Argon2_ds=4
} Argon2_type;

/*************************Argon2 internal data types**************************************************/

/*
 * Structure for the (1KB) memory block implemented as 128 64-bit words.
 * Memory blocks can be copied, XORed. Internal words can be accessed by [] (no bounds checking).
 */
typedef struct _block {
    uint64_t v[ARGON2_WORDS_IN_BLOCK];    
} block;

/*****************Functions that work with the block******************/

//Initialize each byte of the block with @in
extern void InitBlockValue(block* b, uint8_t in);

//Copy block @src to block @dst 
extern void CopyBlock(block* dst, const block* src);

//XOR @src onto @dst bytewise
extern void XORBlock(block* dst, const block* src);


/*
 * Argon2 instance: memory pointer, number of passes, amount of memory, type, and derived values. 
 * Used to evaluate the number and location of blocks to construct in each thread
 */
typedef struct _Argon2_instance_t {
    block* memory; //Memory pointer
    const uint32_t passes; //Number of passes
    const uint32_t memory_blocks; //Number of blocks in memory
    const uint32_t segment_length;
    const uint32_t lane_length;
    const uint32_t lanes;
    const uint32_t threads;
    const Argon2_type type;
    uint64_t *Sbox; //S-boxes for Argon2_ds
    const bool print_internals;  //whether to print the memory blocks
}Argon2_instance_t;


/*
 * Argon2 position: where we construct the block right now. Used to distribute work between threads.
 */
typedef struct _Argon2_position_t {
    const uint32_t pass;
    const uint32_t lane;
    const uint8_t slice;
    uint32_t index;
}Argon2_position_t;

/*Struct that holds the inputs for thread handling FillSegment*/
typedef struct _Argon2_thread_data {
    Argon2_instance_t* instance_ptr;
    Argon2_position_t pos;   
}Argon2_thread_data;

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

/* Clears memory
 * @param instance pointer to the current instance
 * @param clear_memory indicates if we clear the memory with zeros.
 */
void ClearMemory(Argon2_instance_t* instance, bool clear);

/* Deallocates memory
 * @param memory pointer to the blocks
 */
void FreeMemory(block* memory);




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

/*
 * Hashes all the inputs into @a blockhash[PREHASH_DIGEST_LENGTH], clears password and secret if needed
 * @param  context  Pointer to the Argon2 internal structure containing memory pointer, and parameters for time and space requirements.
 * @param  blockhash Buffer for pre-hashing digest
 * @param  type Argon2 type
 * @pre    @a blockhash must have at least @a PREHASH_DIGEST_LENGTH bytes allocated
 */
void InitialHash(uint8_t* blockhash,  Argon2_Context* context, Argon2_type type);

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
 * Function that fills the segment using previous segments also from other threads
 * @param instance Pointer to the current instance
 * @param position Current position
 * @pre all block pointers must be valid
 */
extern void FillSegment(const Argon2_instance_t* instance, Argon2_position_t position);

/*
 * Wrapper for FillSegment for <pthread> library
 * @param thread_data Pointer to the structure that holds inputs for FillSegment
 * @pre all block pointers must be valid
 */
void* FillSegmentThr(void* Argon2_thread_data);

/*
 * Function that fills the entire memory t_cost times based on the first two blocks in each lane
 * @param instance Pointer to the current instance
 */
void FillMemoryBlocks(Argon2_instance_t* instance);


/*
 * Function that performs memory-hard hashing with certain degree of parallelism
 * @param  context  Pointer to the Argon2 internal structure
 * @return Error code if smth is wrong, ARGON2_OK otherwise
 */
int Argon2Core(Argon2_Context* context, Argon2_type type);


extern void GenerateSbox(Argon2_instance_t* instance);

#endif
