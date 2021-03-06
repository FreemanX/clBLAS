/* ************************************************************************
 * Copyright 2013 Advanced Micro Devices, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * ************************************************************************/

#include <stdlib.h>             // srand()
#include <string.h>             // memcpy()
#include <gtest/gtest.h>
#include <clBLAS.h>

#include <common.h>
#include <clBLAS-wrapper.h>
#include <BlasBase.h>
#include <nrm2.h>
#include <blas-random.h>

#ifdef PERF_TEST_WITH_ACML
#include <blas-internal.h>
#include <blas-wrapper.h>
#endif

#include "PerformanceTest.h"

using namespace std;
using namespace clMath;

#define CHECK_RESULT(ret)                                                   \
do {                                                                        \
    ASSERT_GE(ret, 0) << "Fatal error: can not allocate resources or "      \
                         "perform an OpenCL request!" << endl;              \
    EXPECT_EQ(0, ret) << "The OpenCL version is slower in the case" <<      \
                         endl;                                              \
} while (0)

namespace clMath {

template <typename ElemType> class Nrm2PerformanceTest : public PerformanceTest
{
public:
    virtual ~Nrm2PerformanceTest();

    virtual int prepare(void);
    virtual nano_time_t etalonPerfSingle(void);
    virtual nano_time_t clblasPerfSingle(void);

    static void runInstance(BlasFunction fn, TestParams *params)
    {
        Nrm2PerformanceTest<ElemType> perfCase(fn, params);
        int ret = 0;
        int opFactor;
        BlasBase *base;

        base = clMath::BlasBase::getInstance();

        opFactor =1;

        if (((fn == FN_DNRM2) || (fn == FN_DZNRM2)) &&
            !base->isDevSupportDoublePrecision()) {

            std::cerr << ">> WARNING: The target device doesn't support native "
                         "double precision floating point arithmetic" <<
                         std::endl << ">> Test skipped" << std::endl;
            return;
        }

        if (!perfCase.areResourcesSufficient(params)) {
            std::cerr << ">> RESOURCE CHECK: Skip due to insufficient resources" <<
                        std::endl;
			return;
        }
        else {
            ret = perfCase.run(opFactor);
        }

        ASSERT_GE(ret, 0) << "Fatal error: can not allocate resources or "
                             "perform an OpenCL request!" << endl;
        EXPECT_EQ(0, ret) << "The OpenCL version is slower in the case" << endl;
    }

private:
    Nrm2PerformanceTest(BlasFunction fn, TestParams *params);

    bool areResourcesSufficient(TestParams *params);

    TestParams params_;
    ElemType *blasX_;
    cl_mem mobjX_;
	cl_mem mobjNRM2_;
	cl_mem scratchBuff;
    size_t  lengthX;
    ::clMath::BlasBase *base_;
};

template <typename ElemType>
Nrm2PerformanceTest<ElemType>::Nrm2PerformanceTest(
    BlasFunction fn,
    TestParams *params) : PerformanceTest(fn,(problem_size_t) ( (params->N)  * sizeof(ElemType) ) ), params_(*params),
                mobjX_(NULL),mobjNRM2_(NULL)
{

    blasX_ = NULL;
	mobjX_= mobjNRM2_= scratchBuff = NULL;
    lengthX = 1 + (params->N - 1) * abs(params_.incx);

    try
    {
        blasX_ = new ElemType[lengthX + params_.offBX];
    }
    catch(bad_alloc& ba) {
        blasX_ = NULL;     // areResourcesSufficient() will handle the rest and return
        mobjX_= mobjNRM2_= scratchBuff = NULL;
        ba = ba;
    }

    base_ = ::clMath::BlasBase::getInstance();
}

template <typename ElemType>
Nrm2PerformanceTest<ElemType>::~Nrm2PerformanceTest()
{
	if(blasX_ != NULL)
    {
        delete[] blasX_;
	}
    if( mobjX_ != NULL )
    {
		clReleaseMemObject(mobjX_);
    }
	if( mobjNRM2_ != NULL )
    {
        clReleaseMemObject(mobjNRM2_);
    }
	if( scratchBuff!= NULL )
    {
        clReleaseMemObject(scratchBuff);
    }

}

/*
 * Check if available OpenCL resources are sufficient to
 * run the test case
 */
template <typename ElemType> bool
Nrm2PerformanceTest<ElemType>::areResourcesSufficient(TestParams *params)
{
    clMath::BlasBase *base;
    size_t gmemSize, allocSize;
    bool ret;
    size_t sizeX, sizeNRM2, sizeScratch;

	if(blasX_ == NULL) {
		return 0;
	}

    base = clMath::BlasBase::getInstance();
    gmemSize = (size_t)base->availGlobalMemSize( 0 );
    allocSize = (size_t)base->maxMemAllocSize();
    sizeX = (lengthX + params->offBX) * sizeof(ElemType);
    sizeScratch = (lengthX * 2) * sizeof(ElemType);
	sizeNRM2 = (1 + params->offa) * sizeof(ElemType);

    ret = ((sizeX < allocSize) && (sizeNRM2 < allocSize) && (sizeScratch < allocSize));
    ret = (ret && ((sizeX + sizeNRM2 + sizeScratch) < gmemSize));

    return ret;
}

template <typename ElemType> int
Nrm2PerformanceTest<ElemType>::prepare(void)
{

    randomVectors(params_.N, (blasX_ + params_.offBX), params_.incx, (ElemType*)NULL, 0, true);

	mobjX_ = base_->createEnqueueBuffer(blasX_, ((lengthX + params_.offBX) * sizeof(ElemType)), 0, CL_MEM_READ_WRITE);
	mobjNRM2_ = base_->createEnqueueBuffer(NULL, ((1 + params_.offa) * sizeof(ElemType)), 0, CL_MEM_READ_WRITE);
	scratchBuff = base_->createEnqueueBuffer(NULL, ((2 * lengthX) * sizeof(ElemType)), 0, CL_MEM_READ_WRITE);

    return ((mobjX_ != NULL) && (mobjNRM2_ != NULL)&& (scratchBuff != NULL) )? 0 : -1;
}

template <typename ElemType> nano_time_t
Nrm2PerformanceTest<ElemType>::etalonPerfSingle(void)
{
    nano_time_t time = 0;

#ifdef PERF_TEST_WITH_ACML

	time = getCurrentTime();
	clMath::blas::nrm2(params_.N, blasX_, params_.offBX, params_.incx);
	time = getCurrentTime() - time;

#endif  // PERF_TEST_WITH_ACML

    return time;
}


template <typename ElemType> nano_time_t
Nrm2PerformanceTest<ElemType>::clblasPerfSingle(void)
{
    nano_time_t time;
    cl_event event;
    cl_int status;
    cl_command_queue queue = base_->commandQueues()[0];

    DataType type;
    type = ( typeid(ElemType) == typeid(float))? TYPE_FLOAT:( typeid(ElemType) == typeid(double))? TYPE_DOUBLE:
										( typeid(ElemType) == typeid(FloatComplex))? TYPE_COMPLEX_FLOAT: TYPE_COMPLEX_DOUBLE;

    event = NULL;
    clFinish( queue);
    time = getCurrentTime();

#define TIMING
#ifdef TIMING
    int iter = 100;
    for ( int i=1; i <= iter; i++)
    {
#endif

        status = (cl_int)clMath::clblas::nrm2( type, params_.N, mobjNRM2_, params_.offa, mobjX_, params_.offBX, params_.incx,
                                                scratchBuff, 1, &queue, 0, NULL, &event);
        if (status != CL_SUCCESS) {
            cerr << "The CLBLAS NRM2 function failed, status = " <<
                    status << endl;

            return NANOTIME_ERR;
        }
#ifdef TIMING
    } // iter loop
    clFinish( queue);
    time = getCurrentTime() - time;
    time /= iter;
#else

    status = flushAll(1, &queue);
    if (status != CL_SUCCESS) {
        cerr << "clFlush() failed, status = " << status << endl;
        return NANOTIME_ERR;
    }

    time = getCurrentTime();
    status = waitForSuccessfulFinish(1, &queue, &event);
    if (status == CL_SUCCESS) {
        time = getCurrentTime() - time;
    }
    else {
        cerr << "Waiting for completion of commands to the queue failed, "
                "status = " << status << endl;
        time = NANOTIME_ERR;
    }
#endif
    return time;
}

} // namespace clMath

TEST_P(NRM2, snrm2)
{
    TestParams params;

    getParams(&params);
    Nrm2PerformanceTest<float>::runInstance(FN_SNRM2, &params);
}


TEST_P(NRM2, dnrm2)
{
    TestParams params;

    getParams(&params);
    Nrm2PerformanceTest<double>::runInstance(FN_DNRM2, &params);
}

TEST_P(NRM2, scnrm2)
{
    TestParams params;

    getParams(&params);
    Nrm2PerformanceTest<FloatComplex>::runInstance(FN_SCNRM2, &params);
}


TEST_P(NRM2, dznrm2)
{
    TestParams params;

    getParams(&params);
    Nrm2PerformanceTest<DoubleComplex>::runInstance(FN_DZNRM2, &params);
}
