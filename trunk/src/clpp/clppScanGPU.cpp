#include "clpp/clppScanGPU.h"

// Next :
// 1 - Allow templating
// 2 - 

int SIMT_SIZE = 32;

#pragma region Constructor

clppScanGPU::clppScanGPU(clppContext* context, unsigned int maxElements)
{
	_clBuffer_values = 0;
	_clBuffer_valuesOut = 0;
	_clBuffer_BlockSums = 0;

	if (!compile(context, "clppScanGPU.cl"))
		return;

	//---- Prepare all the kernels
	cl_int clStatus;

	_kernel_scan_exclusive = clCreateKernel(_clProgram, "kernel__scan_exclusive", &clStatus);
	checkCLStatus(clStatus);

	_kernel_scan_inclusive = clCreateKernel(_clProgram, "kernel__scan_inclusive", &clStatus);
	checkCLStatus(clStatus);

	_kernel_UniformAdd_exclusive = clCreateKernel(_clProgram, "kernel__UniformAdd_exclusive", &clStatus);
	checkCLStatus(clStatus);

	_kernel_UniformAdd_inclusive = clCreateKernel(_clProgram, "kernel__UniformAdd_inclusive", &clStatus);
	checkCLStatus(clStatus);

	//---- Get the workgroup size
	// ATI : Actually the wavefront size is only 64 for the highend cards(48XX, 58XX, 57XX), but 32 for the middleend cards and 16 for the lowend cards.
	// NVidia : 32 threads in a warp
	//clGetKernelWorkGroupInfo(kernel__scanIntra, _context->clDevice, CL_KERNEL_WORK_GROUP_SIZE, sizeof(size_t), &_workgroupSize, 0);
	//clGetKernelWorkGroupInfo(kernel__scanIntra, _context->clDevice, CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE, sizeof(size_t), &_workgroupSize, 0);

	_workgroupSize = SIMT_SIZE;

	//---- Prepare all the buffers
	allocateBlockSums(maxElements);
}

clppScanGPU::~clppScanGPU()
{
	if (_clBuffer_values)
		delete _clBuffer_values;
	if (_clBuffer_valuesOut)
		delete _clBuffer_valuesOut;

	freeBlockSums();
}

#pragma endregion

#pragma region compilePreprocess

string clppScanGPU::compilePreprocess(string kernel)
{
	if (SIMT_SIZE == 32)
		return string("#define SIMT_SIZE 32\n") + kernel;

	return string("#define SIMT_SIZE 64\n") + kernel;
}

#pragma endregion

#pragma region scan

void clppScanGPU::scan()
{
	cl_int clStatus;

	//---- Apply the scan to each level
	cl_mem clValues = _clBuffer_values;
	for(unsigned int i = 0; i < _pass; i++)
	{
		size_t globalWorkSize = {toMultipleOf(_blockSumsSizes[i], _workgroupSize)};
		size_t localWorkSize = {_workgroupSize};

		if (i < 1)
		{
			clStatus = clSetKernelArg(_kernel_scan_exclusive, 0, sizeof(cl_mem), &clValues);
			clStatus |= clSetKernelArg(_kernel_scan_exclusive, 1, sizeof(cl_mem), &_clBuffer_BlockSums[i]);
			clStatus |= clSetKernelArg(_kernel_scan_exclusive, 2, sizeof(cl_mem), &_blockSumsSizes[i]);
			clStatus |= clEnqueueNDRangeKernel(_context->clQueue, _kernel_scan_exclusive, 1, NULL, &globalWorkSize, &localWorkSize, 0, NULL, NULL);
		}
		else
		{
			clStatus = clSetKernelArg(_kernel_scan_inclusive, 0, sizeof(cl_mem), &clValues);
			clStatus |= clSetKernelArg(_kernel_scan_inclusive, 1, sizeof(cl_mem), &_clBuffer_BlockSums[i]);
			clStatus |= clSetKernelArg(_kernel_scan_inclusive, 2, sizeof(cl_mem), &_blockSumsSizes[i]);
			clStatus |= clEnqueueNDRangeKernel(_context->clQueue, _kernel_scan_inclusive, 1, NULL, &globalWorkSize, &localWorkSize, 0, NULL, NULL);
		}
		checkCLStatus(clStatus);

		// Now we process the sums...
		clValues = _clBuffer_BlockSums[i];
    }
	
	cl_kernel kernel;
	for(int i = _pass - 2; i >= 0; i--)
	{
		kernel = (i < 1) ? _kernel_UniformAdd_exclusive : _kernel_UniformAdd_inclusive;

		size_t globalWorkSize = {toMultipleOf(_blockSumsSizes[i] / 2, _workgroupSize / 2)};
		size_t localWorkSize = {_workgroupSize / 2};

        cl_mem dest = (i > 0) ? _clBuffer_BlockSums[i-1] : _clBuffer_values;

		clStatus = clSetKernelArg(kernel, 0, sizeof(cl_mem), &dest);
		clStatus |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &_clBuffer_BlockSums[i]);
		clStatus |= clSetKernelArg(kernel, 2, sizeof(int), &_blockSumsSizes[i]);
		if (i < 1)
			clStatus |= clSetKernelArg(kernel, 3, sizeof(int), &((int*)_values)[0]);
		checkCLStatus(clStatus);

		clStatus = clEnqueueNDRangeKernel(_context->clQueue, kernel, 1, NULL, &globalWorkSize, &localWorkSize, 0, NULL, NULL);
		checkCLStatus(clStatus);
    }
}

#pragma endregion

#pragma region pushDatas

void clppScanGPU::pushDatas(void* values, void* valuesOut, size_t valueSize, size_t datasetSize)
{
	//---- Store some values
	_values = values;
	_valuesOut = valuesOut;
	_valueSize = valueSize;
	_datasetSize = datasetSize;

	//---- Compute the size of the different block we can use for '_datasetSize' (can be < maxElements)
	// Compute the number of levels requested to do the scan
	_pass = 0;
	unsigned int n = _datasetSize;
	do
	{
		n = (n + _workgroupSize - 1) / _workgroupSize; // round up
		_pass++;
	}
	while(n > 1);

	// Compute the block-sum sizes
	n = _datasetSize;
	for(unsigned int i = 0; i < _pass; i++)
	{
		_blockSumsSizes[i] = n;
		n = (n + _workgroupSize - 1) / _workgroupSize; // round up
	}
	_blockSumsSizes[_pass] = n;

	//---- Copy on the device
	cl_int clStatus;
	_clBuffer_values  = clCreateBuffer(_context->clContext, CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR, _valueSize * _datasetSize, _values, &clStatus);
	checkCLStatus(clStatus);
}

void clppScanGPU::pushDatas(cl_mem clBuffer_keys, cl_mem clBuffer_values, size_t datasetSize)
{
}

#pragma endregion

#pragma region popDatas

void clppScanGPU::popDatas()
{
	cl_int clStatus = clEnqueueReadBuffer(_context->clQueue, _clBuffer_values, CL_TRUE, 0, _valueSize * _datasetSize, _valuesOut, 0, NULL, NULL);
	checkCLStatus(clStatus);
}

#pragma endregion

#pragma region allocateBlockSums

void clppScanGPU::allocateBlockSums(unsigned int maxElements)
{
	// Compute the number of buffers we need for the scan
	cl_int clStatus;
	_pass = 0;
	unsigned int n = maxElements;
	do
	{
		n = (n + _workgroupSize - 1) / _workgroupSize; // round up
		_pass++;
	}
	while(n > 1);

	// Allocate the arrays
	_clBuffer_BlockSums = new cl_mem[_pass];
	_blockSumsSizes = new unsigned int[_pass + 1];

	_clBuffer_valuesOut = clCreateBuffer(_context->clContext, CL_MEM_READ_WRITE, sizeof(int) * maxElements, NULL, &clStatus);
	checkCLStatus(clStatus);

	// Create the cl-buffers
	n = maxElements;
	for(unsigned int i = 0; i < _pass; i++)
	{
		_blockSumsSizes[i] = n;

		_clBuffer_BlockSums[i] = clCreateBuffer(_context->clContext, CL_MEM_READ_WRITE, sizeof(int) * n, NULL, &clStatus);
		checkCLStatus(clStatus);

		n = (n + _workgroupSize - 1) / _workgroupSize; // round up
	}
	_blockSumsSizes[_pass] = n;

	checkCLStatus(clStatus);
}

void clppScanGPU::freeBlockSums()
{
	if (!_clBuffer_BlockSums)
		return;

    cl_int clStatus;
    
	for(unsigned int i = 0; i < _pass; i++)
		clStatus = clReleaseMemObject(_clBuffer_BlockSums[i]);

	delete [] _clBuffer_BlockSums;
	delete [] _blockSumsSizes;
	_clBuffer_BlockSums = 0;
	_blockSumsSizes = 0;
}

#pragma endregion