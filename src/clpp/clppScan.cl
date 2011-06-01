//------------------------------------------------------------
// Purpose :
// ---------
// Prefix sum or prefix scan is an operation where each output element contains the sum of all input elements preceding it.
//
// Algorithm :
// -----------
// The parallel prefix sum has two principal parts, the reduce phase (also known as the up-sweep phase) and the down-sweep phase.
//
// In the up-sweep reduction phase we traverse the computation tree from bottom to top, computing partial sums.
// After this phase, the last element of the array contains the total sum.
//
// During the down-sweep phase, we traverse the tree from the root and use the partial sums to build the scan in place.
//
// Because the scan pictured is an exclusive sum, a zero is inserted into the last element before the start of the down-sweep phase.
// This zero is then propagated back to the first element.
//
// In our implementation, each compute unit loads and sums up two elements (for the deepest depth). Each subsequent depth during the up-sweep
// phase is processed by half of the compute units from the deeper level and the other way around for the down-sweep phase.
//
// In order to be able to scan large arrays, i.e. arrays that have many more elements than the maximum size of a work-group, the prefix sum has to be decomposed.
// Each work-group computes the prefix scan of its sub-range and outputs a single number representing the sum of all elements in its sub-range.
// The workgroup sums are scanned using exactly the same algorithm.
// When the number of work-group results reaches the size of a work-group, the process is reversed and the work-group sums are
// propagated to the sub-ranges, where each work-group adds the incoming sum to all its elements, thus producing the final scanned array.
//
// References :
// ------------
// NVIDIA Mark Harris. Parallel prefix sum (scan) with CUDA. April 2007
// http://developer.download.nvidia.com/compute/cuda/1_1/Website/projects/scan/doc/scan.pdf
//
// Other references :
// ------------------
// http://developer.nvidia.com/node/57
//------------------------------------------------------------

#pragma OPENCL EXTENSION cl_amd_printf : enable
#define T int

//------------------------------------------------------------
// kernel__ExclusivePrefixScanSmall
//
// Purpose : do a fast scan on a small chunck of data.
//------------------------------------------------------------

__kernel 
void kernel__ExclusivePrefixScanSmall(
	__global T* input,
	__global T* output,
	__local  T* block,
	const uint length)
{
	int tid = get_local_id(0);
	
	int offset = 1;

    /* Cache the computational window in shared memory */
	block[2*tid]     = input[2*tid];
	block[2*tid + 1] = input[2*tid + 1];	

    /* build the sum in place up the tree */
	for(int d = length>>1; d > 0; d >>=1)
	{
		barrier(CLK_LOCAL_MEM_FENCE);
		
		if(tid<d)
		{
			int ai = offset*(2*tid + 1) - 1;
			int bi = offset*(2*tid + 2) - 1;
			
			block[bi] += block[ai];
		}
		offset *= 2;
	}

    /* scan back down the tree */

    /* clear the last element */
	if(tid == 0)
	{
		block[length - 1] = 0;
	}

    /* traverse down the tree building the scan in the place */
	for(int d = 1; d < length ; d *= 2)
	{
		offset >>=1;
		barrier(CLK_LOCAL_MEM_FENCE);
		
		if(tid < d)
		{
			int ai = offset*(2*tid + 1) - 1;
			int bi = offset*(2*tid + 2) - 1;
			
			float t = block[ai];
			block[ai] = block[bi];
			block[bi] += t;
		}
	}
	
	barrier(CLK_LOCAL_MEM_FENCE);

    /*write the results back to global memory */
	output[2*tid]     = block[2*tid];
	output[2*tid + 1] = block[2*tid + 1];
}

//------------------------------------------------------------
// kernel__ExclusivePrefixScan
//
// Purpose : do a scan on a chunck of data.
//------------------------------------------------------------

#define NUM_BANKS 16 
#define LOG_NUM_BANKS 4 
#ifdef ZERO_BANK_CONFLICTS 
#define CONFLICT_FREE_OFFSET(n) ((n) >> NUM_BANKS + (n) >> (2 * LOG_NUM_BANKS)) 
#else 
#define CONFLICT_FREE_OFFSET(n) ((n) >> LOG_NUM_BANKS) 
#endif

__kernel
void kernel__ExclusivePrefixScan(
	__global const T* values,
	__global T* valuesOut,
	__local T* localBuffer,
	//const uint localBufferSize,
	__global T* blockSums,
	const uint blockSumsSize
	)
{
	const uint gid = get_global_id(0);
	const uint tid = get_local_id(0);
	const uint bid = get_group_id(0);
	
    const uint localBufferSize = get_local_size(0); // Size for 1 scans
    const uint blockSize = localBufferSize << 1;	// Size for the 2 scans	
    int offset = 1;
	
	//512 & 1024
	//printf("%d %d\n", localBufferSize, blockSize);
	
	// We do a scan on 2 values at a time	
    const int tid2_0 = tid << 1; // 2 * tid
    const int tid2_1 = tid2_0 + 1;
	
	const int gid2_0 = gid << 1;
    const int gid2_1 = gid2_0 + 1;

	// Cache the datas in local memory
	localBuffer[tid2_0] = (gid2_0 < blockSumsSize) ? values[gid2_0] : 0;
	localBuffer[tid2_1] = (gid2_1 < blockSumsSize) ? values[gid2_1] : 0;
	
    // bottom-up
    for(uint d = localBufferSize; d > 0; d >>= 1)
	{
        barrier(CLK_LOCAL_MEM_FENCE);
		
        if (tid < d)
		{
            const uint ai = mad24(offset, (tid2_1+0), -1);	// offset*(tid2_0+1)-1 = offset*(tid2_1+0)-1
            const uint bi = mad24(offset, (tid2_1+1), -1);	// offset*(tid2_1+1)-1;
			
            localBuffer[bi] += localBuffer[ai];
        }
        offset <<= 1;
    }

    barrier(CLK_LOCAL_MEM_FENCE);
    if (tid < 1)
	{
		// Store the value in blockSums buffer before making it to 0
        blockSums[bid] = localBuffer[blockSize-1];
		
		//barrier(CLK_LOCAL_MEM_FENCE | CLK_GLOBAL_MEM_FENCE);
		
		// Clear the last element
        localBuffer[blockSize-1] = 0;
    }

    // top-down
    for(uint d = 1; d < blockSize; d <<= 1)
	{
        offset >>= 1;
        barrier(CLK_LOCAL_MEM_FENCE);
        if (tid < d)
		{
            const uint ai = mad24(offset, (tid2_1+0), -1); // offset*(tid2_0+1)-1 = offset*(tid2_1+0)-1
            const uint bi = mad24(offset, (tid2_1+1), -1); // offset*(tid2_1+1)-1;
			
            T tmp = localBuffer[ai];
            localBuffer[ai] = localBuffer[bi];
            localBuffer[bi] += tmp;
        }
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    // Write out
    if (gid2_0 < blockSumsSize)
        valuesOut[gid2_0] = localBuffer[tid2_0];
		
    if (gid2_1 < blockSumsSize)
        valuesOut[gid2_1] = localBuffer[tid2_1];
}

//------------------------------------------------------------
// kernel__ExclusivePrefixScan
//
// Purpose :
// Final step of large-array scan: combine basic inclusive scan with exclusive scan of top elements of input arrays.
//------------------------------------------------------------

__kernel
void kernel__UniformAdd(
	__global T* memOut,
	__global const T* blockSums,
	const uint N
	)
{
    const uint gid = get_global_id(0) * 2;
    const uint tid = get_local_id(0);
    const uint blockId = get_group_id(0);

    __local T localBuffer[1];

    if (tid == 0)
        localBuffer[0] = blockSums[blockId];

    barrier(CLK_LOCAL_MEM_FENCE);

    if (gid < N)
        memOut[gid] += localBuffer[0];
		
    if (gid + 1 < N)
        memOut[gid + 1] += localBuffer[0];
}