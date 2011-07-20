
char clCode_clppSort_RadixSort[]=
"#pragma OPENCL EXTENSION cl_amd_printf : enable\n"
"#define WGZ 32\n"
"#define WGZ_x2 (WGZ*2)\n"
"#define WGZ_x3 (WGZ*3)\n"
"#define WGZ_x4 (WGZ*4)\n"
"#define WGZ_1 (WGZ-1)\n"
"#define WGZ_2 (WGZ-2)\n"
"#define WGZ_x2_1 (WGZ_x2-1)\n"
"#define WGZ_x3_1 (WGZ_x3-1)\n"
"#define WGZ_x4_1 (WGZ_x4-1)\n"
"#ifdef KEYS_ONLY\n"
"#define KEY(DATA) (DATA)\n"
"#else\n"
"#define KEY(DATA) (DATA.x)\n"
"#endif\n"
"#define EXTRACT_KEY_BIT(VALUE,BIT) ((KEY(VALUE)>>BIT)&0x1)\n"
"#define EXTRACT_KEY_4BITS(VALUE,BIT) ((KEY(VALUE)>>BIT)&0xF)\n"
"#define BARRIER_LOCAL barrier(CLK_LOCAL_MEM_FENCE)\n"
"inline\n"
"void exclusive_scan_4(const uint tid, const int4 tid4, __local uint* localBuffer, __local uint* bitsOnCount)\n"
"{\n"
"const int tid2_0 = tid << 1;\n"
"const int tid2_1 = tid2_0 + 1;\n"
"	\n"
"	int offset = 4;\n"
"	//#pragma unroll\n"
"	for (uint d = 16; d > 0; d >>= 1)\n"
"{\n"
"barrier(CLK_LOCAL_MEM_FENCE);\n"
"		\n"
"if (tid < d)\n"
"{\n"
"const uint ai = mad24(offset, (tid2_1+0), -1);	// offset*(tid2_0+1)-1 = offset*(tid2_1+0)-1\n"
"const uint bi = mad24(offset, (tid2_1+1), -1);	// offset*(tid2_1+1)-1;\n"
"			\n"
"localBuffer[bi] += localBuffer[ai];\n"
"}\n"
"		\n"
"		offset <<= 1;\n"
"}\n"
"barrier(CLK_LOCAL_MEM_FENCE);\n"
"if (tid > WGZ_2)\n"
"{\n"
"bitsOnCount[0] = localBuffer[tid4.w];\n"
"localBuffer[tid4.w] = 0;\n"
"}\n"
"	//#pragma unroll\n"
"for (uint d = 1; d < 32; d <<= 1)\n"
"{\n"
"barrier(CLK_LOCAL_MEM_FENCE);\n"
"		offset >>= 1;\n"
"		\n"
"if (tid < d)\n"
"{\n"
"const uint ai = mad24(offset, (tid2_1+0), -1); // offset*(tid2_0+1)-1 = offset*(tid2_1+0)-1\n"
"const uint bi = mad24(offset, (tid2_1+1), -1); // offset*(tid2_1+1)-1;\n"
"			\n"
"uint tmp = localBuffer[ai];\n"
"localBuffer[ai] = localBuffer[bi];\n"
"localBuffer[bi] += tmp;\n"
"}\n"
"}\n"
"barrier(CLK_LOCAL_MEM_FENCE);\n"
"}\n"
"inline \n"
"void exclusive_scan_128(const uint tid, const int4 tid4, __local uint* localBuffer, __local uint* bitsOnCount)\n"
"{\n"
"	// local serial reduction\n"
"	localBuffer[tid4.y] += localBuffer[tid4.x];\n"
"	localBuffer[tid4.w] += localBuffer[tid4.z];\n"
"	localBuffer[tid4.w] += localBuffer[tid4.y];\n"
"		\n"
"	// Exclusive scan starting with an offset of 4\n"
"	exclusive_scan_4(tid, tid4, localBuffer, bitsOnCount);\n"
"		\n"
"	// local 4-element serial expansion\n"
"	uint tmp;\n"
"	tmp = localBuffer[tid4.y];    localBuffer[tid4.y] = localBuffer[tid4.w];  localBuffer[tid4.w] += tmp;\n"
"	tmp = localBuffer[tid4.x];    localBuffer[tid4.x] = localBuffer[tid4.y];  localBuffer[tid4.y] += tmp;\n"
"	tmp = localBuffer[tid4.z];    localBuffer[tid4.z] = localBuffer[tid4.w];  localBuffer[tid4.w] += tmp;\n"
"}\n"
"__kernel\n"
"void kernel__radixLocalSort(\n"
"	__local KV_TYPE* localData,			// size 4*4 int2s (8 kB)\n"
"	__global KV_TYPE* data,				// size 4*4 int2s per block (8 kB)\n"
"	const int bitOffset,				// k*4, k=0..7\n"
"	const int N)						// Total number of items to sort\n"
"{\n"
"	const int tid = (int)get_local_id(0);\n"
"		\n"
"const int4 gid4 = (int4)(get_global_id(0) << 2) + (const int4)(0,1,2,3);    \n"
"const int4 tid4 = (int4)(tid << 2) + (const int4)(0,1,2,3);\n"
"	// Local memory\n"
"	__local uint localBitsScan[WGZ_x4];\n"
"__local uint bitsOnCount[1];\n"
"localData[tid4.x] = (gid4.x < N) ? data[gid4.x] : MAX_KV_TYPE;\n"
"localData[tid4.y] = (gid4.y < N) ? data[gid4.y] : MAX_KV_TYPE;\n"
"localData[tid4.z] = (gid4.z < N) ? data[gid4.z] : MAX_KV_TYPE;\n"
"localData[tid4.w] = (gid4.w < N) ? data[gid4.w] : MAX_KV_TYPE;\n"
"	\n"
"	//-------- 1) 4 x local 1-bit split\n"
"	__local KV_TYPE* localTemp = localData + WGZ_x4;\n"
"	#pragma unroll // SLOWER on some cards!!\n"
"for(uint shift = bitOffset; shift < (bitOffset+4); shift++) // Radix 4\n"
"{\n"
"		BARRIER_LOCAL;\n"
"		\n"
"		//---- Setup the array of 4 bits (of level shift)\n"
"		// Create the '1s' array as explained at : http://http.developer.nvidia.com/GPUGems3/gpugems3_ch39.html\n"
"		// In fact we simply inverse the bits	\n"
"		// Local copy and bits extraction\n"
"		int4 flags;\n"
"		flags.x = localBitsScan[tid4.x] = ! EXTRACT_KEY_BIT(localData[tid4.x], shift);\n"
"flags.y = localBitsScan[tid4.y] = ! EXTRACT_KEY_BIT(localData[tid4.y], shift);\n"
"flags.z = localBitsScan[tid4.z] = ! EXTRACT_KEY_BIT(localData[tid4.z], shift);\n"
"flags.w = localBitsScan[tid4.w] = ! EXTRACT_KEY_BIT(localData[tid4.w], shift);\n"
"						\n"
"		//---- Do a scan of the 128 bits and retreive the total number of '1' in 'bitsOnCount'\n"
"		exclusive_scan_128(tid, tid4, localBitsScan, bitsOnCount);\n"
"		\n"
"		BARRIER_LOCAL;\n"
"		\n"
"		//----\n"
"		int offset;\n"
"		int4 invFlags = 1 - flags;\n"
"		\n"
"		offset = invFlags.x * (bitsOnCount[0] + tid4.x - localBitsScan[tid4.x]) + flags.x * localBitsScan[tid4.x];\n"
"		localTemp[offset] = localData[tid4.x];\n"
"		\n"
"		offset = invFlags.y * (bitsOnCount[0] + tid4.y - localBitsScan[tid4.y]) + flags.y * localBitsScan[tid4.y];\n"
"		localTemp[offset] = localData[tid4.y];\n"
"		\n"
"		offset = invFlags.z * (bitsOnCount[0] + tid4.z - localBitsScan[tid4.z]) + flags.z * localBitsScan[tid4.z];\n"
"		localTemp[offset] = localData[tid4.z];\n"
"				\n"
"		offset = invFlags.w * (bitsOnCount[0] + tid4.w - localBitsScan[tid4.w]) + flags.w * localBitsScan[tid4.w];\n"
"		localTemp[offset] = localData[tid4.w];\n"
"		\n"
"		BARRIER_LOCAL;\n"
"		// Swap the buffer pointers\n"
"		__local KV_TYPE* swBuf = localData;\n"
"		localData = localTemp;\n"
"		localTemp = swBuf;\n"
"		\n"
"		//barrier(CLK_LOCAL_MEM_FENCE); // NO CRASH !!			\n"
"}\n"
"	\n"
"	// FASTER !!\n"
"	//barrier(CLK_LOCAL_MEM_FENCE); // NO CRASH !!\n"
"	\n"
"	// Write sorted data back to global memory\n"
"	if (gid4.x < N) data[gid4.x] = localData[tid4.x];\n"
"if (gid4.y < N) data[gid4.y] = localData[tid4.y];\n"
"if (gid4.z < N) data[gid4.z] = localData[tid4.z];\n"
"if (gid4.w < N) data[gid4.w] = localData[tid4.w];	\n"
"}\n"
"__kernel\n"
"void kernel__localHistogram(__global KV_TYPE* data, const int bitOffset, __global int* hist, __global int* blockHists, const int N)\n"
"{\n"
"const int tid = (int)get_local_id(0);\n"
"const int4 tid4 = (int4)(tid << 2) + (const int4)(0,1,2,3);\n"
"	const int4 gid4 = (int4)(get_global_id(0) << 2) + (const int4)(0,1,2,3);\n"
"	const int blockId = (int)get_group_id(0);\n"
"	\n"
"	__local uint localData[WGZ*4];\n"
"__local int localHistStart[16];\n"
"__local int localHistEnd[16];\n"
"	\n"
"localData[tid4.x] = (gid4.x < N) ? EXTRACT_KEY_4BITS(data[gid4.x], bitOffset) : EXTRACT_KEY_4BITS(MAX_KV_TYPE, bitOffset);\n"
"localData[tid4.y] = (gid4.y < N) ? EXTRACT_KEY_4BITS(data[gid4.y], bitOffset) : EXTRACT_KEY_4BITS(MAX_KV_TYPE, bitOffset);\n"
"localData[tid4.z] = (gid4.z < N) ? EXTRACT_KEY_4BITS(data[gid4.z], bitOffset) : EXTRACT_KEY_4BITS(MAX_KV_TYPE, bitOffset);\n"
"localData[tid4.w] = (gid4.w < N) ? EXTRACT_KEY_4BITS(data[gid4.w], bitOffset) : EXTRACT_KEY_4BITS(MAX_KV_TYPE, bitOffset);\n"
"	\n"
"	//-------- 2) Histogram\n"
"BARRIER_LOCAL;\n"
"if (tid < 16)\n"
"{\n"
"localHistStart[tid] = 0;\n"
"localHistEnd[tid] = -1;\n"
"}\n"
"	BARRIER_LOCAL;\n"
"if (tid4.x > 0 && localData[tid4.x] != localData[tid4.x-1])\n"
"{\n"
"		localHistStart[localData[tid4.x]] = tid4.x;\n"
"localHistEnd[localData[tid4.x-1]] = tid4.x - 1;        \n"
"}\n"
"if (localData[tid4.y] != localData[tid4.x])\n"
"{\n"
"localHistEnd[localData[tid4.x]] = tid4.x;\n"
"localHistStart[localData[tid4.y]] = tid4.y;\n"
"}\n"
"if (localData[tid4.z] != localData[tid4.y])\n"
"{\n"
"localHistEnd[localData[tid4.y]] = tid4.y;\n"
"localHistStart[localData[tid4.z]] = tid4.z;\n"
"}\n"
"if (localData[tid4.w] != localData[tid4.z])\n"
"{\n"
"localHistEnd[localData[tid4.z]] = tid4.z;\n"
"localHistStart[localData[tid4.w]] = tid4.w;\n"
"}\n"
"if (tid < 1)\n"
"{\n"
"		localHistEnd[localData[WGZ_x4-1]] = WGZ_x4 - 1;\n"
"		localHistStart[localData[0]] = 0;\n"
"}\n"
"BARRIER_LOCAL;\n"
"if (tid < 16)\n"
"{\n"
"hist[tid * get_num_groups(0) + blockId] = localHistEnd[tid] - localHistStart[tid] + 1;\n"
"		blockHists[(blockId << 5) + tid] = localHistStart[tid];\n"
"}\n"
"}\n"
"__kernel\n"
"void kernel__radixPermute(\n"
"	__global const KV_TYPE* dataIn,		// size 4*4 int2s per block\n"
"	__global KV_TYPE* dataOut,			// size 4*4 int2s per block\n"
"	__global const int* histSum,		// size 16 per block (64 B)\n"
"	__global const int* blockHists,		// size 16 int2s per block (64 B)\n"
"	const uint bitOffset,				// k*4, k=0..7\n"
"	const uint N,\n"
"	const int numBlocks)\n"
"{\n"
"const uint4 gid4 = ((const uint4)(get_global_id(0) << 2)) + (const uint4)(0,1,2,3);\n"
"const uint tid = get_local_id(0);\n"
"const uint4 tid4 = ((const uint4)(tid << 2)) + (const uint4)(0,1,2,3);\n"
"__local int sharedHistSum[16];\n"
"__local int localHistStart[16];\n"
"if (tid < 16)\n"
"{\n"
"		const uint blockId = get_group_id(0);\n"
"sharedHistSum[tid] = histSum[tid * numBlocks + blockId];\n"
"		//localHistStart[tid] = blockHists[(blockId << 4) + tid];\n"
"localHistStart[tid] = blockHists[(blockId << 5) + tid];\n"
"}\n"
"	\n"
"	BARRIER_LOCAL;\n"
"KV_TYPE myData[4];\n"
"uint myShiftedKeys[4];\n"
"myData[0] = (gid4.x < N) ? dataIn[gid4.x] : MAX_KV_TYPE;\n"
"myData[1] = (gid4.y < N) ? dataIn[gid4.y] : MAX_KV_TYPE;\n"
"myData[2] = (gid4.z < N) ? dataIn[gid4.z] : MAX_KV_TYPE;\n"
"myData[3] = (gid4.w < N) ? dataIn[gid4.w] : MAX_KV_TYPE;\n"
"myShiftedKeys[0] = EXTRACT_KEY_4BITS(myData[0], bitOffset);\n"
"myShiftedKeys[1] = EXTRACT_KEY_4BITS(myData[1], bitOffset);\n"
"myShiftedKeys[2] = EXTRACT_KEY_4BITS(myData[2], bitOffset);\n"
"myShiftedKeys[3] = EXTRACT_KEY_4BITS(myData[3], bitOffset);\n"
"	// Necessary ?\n"
"uint4 finalOffset;\n"
"finalOffset.x = tid4.x - localHistStart[myShiftedKeys[0]] + sharedHistSum[myShiftedKeys[0]];\n"
"finalOffset.y = tid4.y - localHistStart[myShiftedKeys[1]] + sharedHistSum[myShiftedKeys[1]];\n"
"finalOffset.z = tid4.z - localHistStart[myShiftedKeys[2]] + sharedHistSum[myShiftedKeys[2]];\n"
"finalOffset.w = tid4.w - localHistStart[myShiftedKeys[3]] + sharedHistSum[myShiftedKeys[3]];\n"
"	if (finalOffset.x < N) dataOut[finalOffset.x] = myData[0];\n"
"if (finalOffset.y < N) dataOut[finalOffset.y] = myData[1];\n"
"if (finalOffset.z < N) dataOut[finalOffset.z] = myData[2];\n"
"if (finalOffset.w < N) dataOut[finalOffset.w] = myData[3];\n"
"}\n"
;
