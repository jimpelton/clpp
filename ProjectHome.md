clpp is an OpenCL Data Parallel Primitives Library. It is a library of data-parallel algorithm primitives such as parallel-prefix-sum ("scan"), parallel sort and parallel reduction. Primitives such as these are important building blocks for a wide variety of data-parallel algorithms, including sorting, stream compaction, and building data structures such as trees and summed-area tables.

If you want to join the project, please simply send a message to our mailing list : http://groups.google.com/group/cl-pp


---

# Sponsor #
The CLPP project is a general purpose framework for any OpenCL related software. It is the reason we are searching for one or several sponsors to help us to develop/test/improve the current library code. Any Company, University, Institutes, Research Lab or even individuals are welcomed.


---

# Algorithms for v1.0 #
  1. Scan/Reduction
  1. Sort
  1. Sort Key + Value


---

# Roadmap for v1.1 #
  1. Templating : allowing to use float/int/... or any structure/type in the algorithms
  1. Multiple scan
  1. Stream compaction
  1. Area summed table
  1. Split


---

# Optimization roadmap v1.x #

We have no plan for this optimizations but we expect to implement them asap.
  1. Implement the Duane Merrill Radix sort
  1. Implement the Duane Merril scan
  1. Test/optimize the algorithms on Fusion and Sandy Bridge


---

# Example #

The following source code simply sort a set of "int" values.

```
#include <clpp/clpp.h>

int main(void)
{
	clppProgram::setBasePath("src/clpp/");
	
	//---- Prepare a clpp Context
	clppContext context;
	context.setup(0, 0);
	
	//---- Create the sort
	clppSort sort = clpp::createBestSort(context, 1000000);
	
	int* dataToSort = ...
	sort->pushDatas(dataToSort, 1000000, 32);
	
	sort->sort();
	sort->waitCompletion();
	
	sort->popDatas();

	delete sort();

	return 0;
}
```


---


# Benchmarks #

<img src='http://clpp.googlecode.com/svn/trunk/report/Benchmark.png' height='498' width='800' />