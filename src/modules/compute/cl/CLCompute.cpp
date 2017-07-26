/**
 * @file
 */
#include "CL.h"
#include "CLMapping.h"
#include "compute/Compute.h"
#include "core/Log.h"
#include "core/App.h"
#include "io/Filesystem.h"
// TODO: don't link against OpenCL - dyload it (flextgl)
#include <vector>
#include <string>

namespace compute {

namespace _priv {
struct Context {
	cl_uint platformIdCount = 0;
	std::vector<cl_platform_id> platformIds;
	cl_uint deviceIdCount = 0;
	std::vector<cl_device_id> deviceIds;
	cl_context context = nullptr;
	cl_command_queue commandQueue = nullptr;
	cl_device_id deviceId = nullptr;
};

static Context _ctx;

}

#ifdef DEBUG
static const char *convertCLError(cl_int err) {
	switch (err) {
	case CL_SUCCESS:
		return "Success";
	case CL_DEVICE_NOT_FOUND:
		return "Device not found";
	case CL_DEVICE_NOT_AVAILABLE:
		return "Device not available";
	case CL_COMPILER_NOT_AVAILABLE:
		return "Compiler not available";
	case CL_MEM_OBJECT_ALLOCATION_FAILURE:
		return "Memory object allocation failure";
	case CL_OUT_OF_RESOURCES:
		return "Out of resources";
	case CL_OUT_OF_HOST_MEMORY:
		return "Out of host memory";
	case CL_PROFILING_INFO_NOT_AVAILABLE:
		return "Profiling information not available";
	case CL_MEM_COPY_OVERLAP:
		return "Memory copy overlap";
	case CL_IMAGE_FORMAT_MISMATCH:
		return "Image format mismatch";
	case CL_IMAGE_FORMAT_NOT_SUPPORTED:
		return "Image format not supported";
	case CL_BUILD_PROGRAM_FAILURE:
		return "Program build failure";
	case CL_MAP_FAILURE:
		return "Map failure";
	case CL_INVALID_VALUE:
		return "Invalid value";
	case CL_INVALID_DEVICE_TYPE:
		return "Invalid device type";
	case CL_INVALID_PLATFORM:
		return "Invalid platform";
	case CL_INVALID_DEVICE:
		return "Invalid device";
	case CL_INVALID_CONTEXT:
		return "Invalid context";
	case CL_INVALID_QUEUE_PROPERTIES:
		return "Invalid queue properties";
	case CL_INVALID_COMMAND_QUEUE:
		return "Invalid command queue";
	case CL_INVALID_HOST_PTR:
		return "Invalid host pointer";
	case CL_INVALID_MEM_OBJECT:
		return "Invalid memory object";
	case CL_INVALID_IMAGE_FORMAT_DESCRIPTOR:
		return "Invalid image format descriptor";
	case CL_INVALID_IMAGE_SIZE:
		return "Invalid image size";
	case CL_INVALID_SAMPLER:
		return "Invalid sampler";
	case CL_INVALID_BINARY:
		return "Invalid binary";
	case CL_INVALID_BUILD_OPTIONS:
		return "Invalid build options";
	case CL_INVALID_PROGRAM:
		return "Invalid program";
	case CL_INVALID_PROGRAM_EXECUTABLE:
		return "Invalid program executable";
	case CL_INVALID_KERNEL_NAME:
		return "Invalid kernel name";
	case CL_INVALID_KERNEL_DEFINITION:
		return "Invalid kernel definition";
	case CL_INVALID_KERNEL:
		return "Invalid kernel";
	case CL_INVALID_ARG_INDEX:
		return "Invalid argument index";
	case CL_INVALID_ARG_VALUE:
		return "Invalid argument value";
	case CL_INVALID_ARG_SIZE:
		return "Invalid argument size";
	case CL_INVALID_KERNEL_ARGS:
		return "Invalid kernel arguments";
	case CL_INVALID_WORK_DIMENSION:
		return "Invalid work dimension";
	case CL_INVALID_WORK_GROUP_SIZE:
		return "Invalid work group size";
	case CL_INVALID_WORK_ITEM_SIZE:
		return "Invalid work item size";
	case CL_INVALID_GLOBAL_OFFSET:
		return "Invalid global offset";
	case CL_INVALID_EVENT_WAIT_LIST:
		return "Invalid event wait list";
	case CL_INVALID_EVENT:
		return "Invalid event";
	case CL_INVALID_OPERATION:
		return "Invalid operation";
	case CL_INVALID_GL_OBJECT:
		return "Invalid OpenGL object";
	case CL_INVALID_BUFFER_SIZE:
		return "Invalid buffer size";
	case CL_INVALID_MIP_LEVEL:
		return "Invalid mip-map level";
	default:
		return "Unknown";
	}
}
#endif

#ifdef DEBUG
#define checkError(clError) core_assert_msg(clError == CL_SUCCESS, "CL err: %s => %i", convertCLError(clError), clError)
#else
#define checkError(clError)
#endif

static std::string getPlatformName(cl_platform_id id) {
	size_t size = 0u;
	cl_int error;

	error = clGetPlatformInfo(id, CL_PLATFORM_NAME, 0, nullptr, &size);
	checkError(error);

	std::string result;
	result.resize(size);
	error = clGetPlatformInfo(id, CL_PLATFORM_NAME, size,
			const_cast<char*>(result.data()), nullptr);
	checkError(error);

	return result;
}

static std::string getDeviceName(cl_device_id id) {
	size_t size = 0u;
	cl_int error;

	error = clGetDeviceInfo(id, CL_DEVICE_NAME, 0, nullptr, &size);
	checkError(error);

	std::string result;
	result.resize(size);
	error = clGetDeviceInfo(id, CL_DEVICE_NAME, size,
			const_cast<char*>(result.data()), nullptr);
	checkError(error);

	return result;
}

bool configureProgram(Id program) {
	const cl_int error = clBuildProgram((cl_program)program,
		_priv::_ctx.deviceIdCount,
		_priv::_ctx.deviceIds.data(),
		"",
		nullptr,
		nullptr);
	if (error == CL_SUCCESS) {
		return true;
	}
	char buf[4096];
	clGetProgramBuildInfo((cl_program) program, _priv::_ctx.deviceId,
			CL_PROGRAM_BUILD_LOG, sizeof(buf), buf, nullptr);
	Log::error("Failed to build program: %s", buf);
	checkError(error);
	return false;
}

bool deleteProgram(Id& program) {
	if (program == InvalidId) {
		return true;
	}
	const cl_int error = clReleaseProgram((cl_program)program);
	checkError(error);
	if (error == CL_SUCCESS) {
		program = InvalidId;
		return true;
	}
	return false;
}

Id createBuffer(BufferFlag flags, size_t size, void* data) {
	core_assert(_priv::_ctx.context != nullptr);
	cl_int error;

	cl_mem_flags clValue = 0;
	if ((flags & BufferFlag::ReadWrite) != BufferFlag::None) {
		clValue |= CL_MEM_READ_WRITE;
	}
	if ((flags & BufferFlag::WriteOnly) != BufferFlag::None) {
		clValue |= CL_MEM_WRITE_ONLY;
	}
	if ((flags & BufferFlag::ReadOnly) != BufferFlag::None) {
		clValue |= CL_MEM_READ_ONLY;
	}
	if ((flags & BufferFlag::UseHostPointer) != BufferFlag::None) {
		clValue |= CL_MEM_USE_HOST_PTR;
	}
	if ((flags & BufferFlag::AllocHostPointer) != BufferFlag::None) {
		clValue |= CL_MEM_ALLOC_HOST_PTR;
	}
	if ((flags & BufferFlag::CopyHostPointer) != BufferFlag::None) {
		clValue |= CL_MEM_COPY_HOST_PTR;
	}

	const bool useHostPtr = (flags & BufferFlag::UseHostPointer) != BufferFlag::None;

	cl_mem bufferObject = clCreateBuffer(_priv::_ctx.context, clValue,
			size, useHostPtr ? data : nullptr, &error);
	checkError(error);
	if (error != CL_SUCCESS) {
		return InvalidId;
	}
	if (!useHostPtr) {
		void *target = clEnqueueMapBuffer(_priv::_ctx.commandQueue, bufferObject, CL_TRUE, CL_MAP_WRITE,
				0, size, 0, nullptr, nullptr, &error);
		checkError(error);
		if (target == nullptr) {
			return InvalidId;
		}
		memcpy(target, data, size);
		cl_event event;
		error = clEnqueueUnmapMemObject(_priv::_ctx.commandQueue, bufferObject, target, 0, nullptr, &event);
		checkError(error);
		if (error != CL_SUCCESS) {
			return InvalidId;
		}
		error = clWaitForEvents(1, &event);
		checkError(error);
	}
	return (Id)bufferObject;
}

bool deleteBuffer(Id& buffer) {
	if (buffer == InvalidId) {
		return true;
	}
	const cl_int error = clReleaseMemObject((cl_mem)buffer);
	checkError(error);
	if (error == CL_SUCCESS) {
		buffer = InvalidId;
		return true;
	}
	return false;
}

bool updateBuffer(Id buffer, size_t size, const void* data, bool blockingWrite) {
	if (buffer == InvalidId) {
		return false;
	}
	core_assert(_priv::_ctx.commandQueue != nullptr);
	const cl_int error = clEnqueueWriteBuffer(_priv::_ctx.commandQueue,
			(cl_mem) buffer, blockingWrite ? CL_TRUE : CL_FALSE, 0, size, data,
			0, nullptr, nullptr);
	checkError(error);
	return error == CL_SUCCESS;
}

bool readBuffer(Id buffer, size_t size, void* data) {
	if (buffer == InvalidId) {
		return false;
	}
	core_assert(_priv::_ctx.commandQueue != nullptr);
	const cl_int error = clEnqueueReadBuffer(_priv::_ctx.commandQueue,
			(cl_mem) buffer, CL_TRUE, 0, size, data, 0, nullptr, nullptr);
	checkError(error);
	return error == CL_SUCCESS;
}

Id createProgram(const std::string& source) {
	// http://www.khronos.org/registry/cl/sdk/1.1/docs/man/xhtml/clCreateProgramWithSource.html
	const size_t lengths[] = { source.size () };
	const char* sources[] = { source.data () };

	cl_int error = CL_SUCCESS;
	cl_program program = clCreateProgramWithSource(_priv::_ctx.context,
			SDL_arraysize(sources),
			sources,
			lengths,
			&error);
	checkError(error);
	return program;
}

bool deleteKernel(Id& kernel) {
	if (kernel == InvalidId) {
		return false;
	}
	const cl_int error = clReleaseKernel((cl_kernel)kernel);
	checkError(error);
	if (error == CL_SUCCESS) {
		kernel = InvalidId;
		return true;
	}
	return false;
}

bool kernelArg(Id kernel, uint32_t index, size_t size, const void* data) {
	if (kernel == InvalidId) {
		return false;
	}
	const cl_int error = clSetKernelArg((cl_kernel)kernel, index, size, data);
	checkError(error);
	return error == CL_SUCCESS;
}

/**
 * Work-group instances are executed in parallel across multiple compute units or concurrently
 * on the same compute unit.
 *
 * Each work-item is uniquely identified by a global identifier. The global ID, which can be read
 * inside the kernel, is computed using the value given by global_work_size and global_work_offset.
 * In OpenCL 1.0, the starting global ID is always (0, 0, ... 0). In addition, a work-item is also
 * identified within a work-group by a unique local ID. The local ID, which can also be read by the
 * kernel, is computed using the value given by local_work_size. The starting local ID is
 * always (0, 0, ... 0).
 *
 * @param[in] kernel A valid kernel object. The OpenCL context associated with kernel and commandQueue
 * must be the same.
 *
 * @param[in] workDim The number of dimensions used to specify the global work-items and work-items in the
 * work-group. work_dim must be greater than zero and less than or equal to three.
 * get_global_id(X) where the highest X is the workDim
 *
 * @param[in] workSize Points to an array of work_dim unsigned values that describe the number of global work-items
 * in work_dim dimensions that will execute the kernel function. The total number of global
 * work-items is computed as global_work_size[0] *...* global_work_size[work_dim - 1].
 * The values specified in global_work_size cannot exceed the range given by the sizeof(size_t)
 * for the device on which the kernel execution will be enqueued. The sizeof(size_t) for a
 * device can be determined using CL_DEVICE_ADDRESS_BITS in the table of OpenCL Device Queries
 * for clGetDeviceInfo. If, for example, CL_DEVICE_ADDRESS_BITS = 32, i.e. the device uses a
 * 32-bit address space, size_t is a 32-bit unsigned integer and global_work_size values must
 * be in the range 1 .. 2^32 - 1. Values outside this range return a CL_OUT_OF_RESOURCES error.
 */
bool kernelRun(Id kernel, int workSize, int workDim, bool blocking) {
	if (kernel == InvalidId) {
		return false;
	}
	/**
	 * Returns an event object that identifies this particular kernel execution instance.
	 * Event objects are unique and can be used to identify a particular kernel execution
	 * instance later on. If event is NULL, no event will be created for this kernel
	 * execution instance and therefore it will not be possible for the application to
	 * query or queue a wait for this particular kernel execution instance.
	 */
	cl_event event = nullptr;
	cl_kernel clKernel = (cl_kernel) kernel;
	/**
	 * Must currently be a NULL value. In a future revision of OpenCL, global_work_offset can
	 * be used to specify an array of work_dim unsigned values that describe the offset used
	 * to calculate the global ID of a work-item instead of having the global IDs always start
	 * at offset (0, 0,... 0).
	 */
	const size_t *globalWorkOffset = nullptr;
	const size_t globalWorkSize = workSize;
	/**
	 * Points to an array of work_dim unsigned values that describe the number of work-items that
	 * make up a work-group (also referred to as the size of the work-group) that will execute the
	 * kernel specified by kernel. The total number of work-items in a work-group is computed as
	 * @c localWorkSize[0] *... * localWorkSize[work_dim - 1]. The total number of work-items in
	 * the work-group must be less than or equal to the CL_DEVICE_MAX_WORK_GROUP_SIZE value specified
	 * in table of OpenCL Device Queries for clGetDeviceInfo and the number of work-items specified
	 * in local_work_size[0],... local_work_size[work_dim - 1] must be less than or equal to the
	 * corresponding values specified
	 * by CL_DEVICE_MAX_WORK_ITEM_SIZES[0],.... CL_DEVICE_MAX_WORK_ITEM_SIZES[work_dim - 1].
	 * The explicitly specified local_work_size will be used to determine how to break the global
	 * work-items specified by global_work_size into appropriate work-group instances. If
	 * local_work_size is specified, the values specified in
	 * global_work_size[0],... global_work_size[work_dim - 1] must be evenly divisable by the
	 * corresponding values specified in local_work_size[0],... local_work_size[work_dim - 1].
	 *
	 * The work-group size to be used for kernel can also be specified in the program source using
	 * the __attribute__((reqd_work_group_size(X, Y, Z)))qualifier. In this case the size of work
	 * group specified by local_work_size must match the value specified by the
	 * reqd_work_group_size __attribute__ qualifier.
	 *
	 * @c localWorkSize can also be a NULL value in which case the OpenCL implementation will
	 * determine how to be break the global work-items into appropriate work-group instances.
	 */
	const size_t *localWorkSize = nullptr;
	/**
	 * Specify events that need to complete before this particular command can be executed. If
	 * event_wait_list is NULL, then this particular command does not wait on any event to complete.
	 * If event_wait_list is NULL, num_events_in_wait_list must be 0. If event_wait_list is not NULL,
	 * the list of events pointed to by event_wait_list must be valid and num_events_in_wait_list
	 * must be greater than 0. The events specified in event_wait_list act as synchronization points.
	 * The context associated with events in event_wait_list and command_queue must be the same.
	 */
	const cl_uint numEventsInWaitList = 0;
	const cl_event* eventWaitList = nullptr;

	core_assert(_priv::_ctx.commandQueue != nullptr);
	const cl_int error = clEnqueueNDRangeKernel(_priv::_ctx.commandQueue,
			clKernel, workDim, globalWorkOffset, &globalWorkSize,
			localWorkSize, numEventsInWaitList, eventWaitList, &event);
	checkError(error);
	if (error == CL_SUCCESS) {
		if (blocking) {
			clFinish(_priv::_ctx.commandQueue);
		}
		return true;
	}
	return false;
}

Id createKernel(Id program, const char *name) {
	if (program == InvalidId) {
		return InvalidId;
	}
	core_assert(name != nullptr);
	// http://www.khronos.org/registry/cl/sdk/1.1/docs/man/xhtml/clCreateKernel.html
	cl_int error = CL_SUCCESS;
	cl_kernel kernel = clCreateKernel((cl_program)program, name, &error);
	checkError(error);
	if (error == CL_SUCCESS) {
		return kernel;
	}
	return InvalidId;
}

bool init() {
	cl_int error;
	// http://www.khronos.org/registry/cl/sdk/1.1/docs/man/xhtml/clGetPlatformIDs.html
	// http://www.khronos.org/registry/cl/sdk/1.1/docs/man/xhtml/clGetDeviceIDs.html
	// http://www.khronos.org/registry/cl/sdk/1.1/docs/man/xhtml/clCreateContext.html
	error = clGetPlatformIDs(0, nullptr, &_priv::_ctx.platformIdCount);
	checkError(error);

	if (_priv::_ctx.platformIdCount == 0u) {
		Log::error("No OpenCL platform found");
		return false;
	} else {
		Log::info("Found %u platform(s)", _priv::_ctx.platformIdCount);
	}

	_priv::_ctx.platformIds.reserve(_priv::_ctx.platformIdCount);
	error = clGetPlatformIDs(_priv::_ctx.platformIdCount,
			_priv::_ctx.platformIds.data(), nullptr);
	checkError(error);

	for (cl_uint i = 0; i < _priv::_ctx.platformIdCount; ++i) {
		const std::string& platform = getPlatformName(_priv::_ctx.platformIds[i]);
		Log::info("* (%i): %s", i + 1, platform.c_str());
	}

	error = clGetDeviceIDs(_priv::_ctx.platformIds[0], CL_DEVICE_TYPE_ALL, 0,
			nullptr, &_priv::_ctx.deviceIdCount);
	checkError(error);

	if (_priv::_ctx.deviceIdCount == 0u) {
		Log::error("No OpenCL devices found");
		return false;
	} else {
		Log::info("Found %u device(s)", _priv::_ctx.deviceIdCount);
	}

	_priv::_ctx.deviceIds.reserve(_priv::_ctx.deviceIdCount);
	error = clGetDeviceIDs(_priv::_ctx.platformIds[0], CL_DEVICE_TYPE_ALL,
			_priv::_ctx.deviceIdCount, _priv::_ctx.deviceIds.data(), nullptr);
	checkError(error);

	for (cl_uint i = 0; i < _priv::_ctx.deviceIdCount; ++i) {
		const std::string& device = getDeviceName(_priv::_ctx.deviceIds[i]);
		Log::info("* (%i): %s", i + 1, device.c_str());
	}

	const cl_context_properties contextProperties[] = {
			CL_CONTEXT_PLATFORM,
			reinterpret_cast<cl_context_properties>(_priv::_ctx.platformIds[0]),
			0,
			0 };

	error = clGetDeviceIDs(_priv::_ctx.platformIds[0], CL_DEVICE_TYPE_DEFAULT,
			1, &_priv::_ctx.deviceId, nullptr);
	checkError(error);

	error = CL_SUCCESS;
	_priv::_ctx.context = clCreateContext(contextProperties, 1,
			&_priv::_ctx.deviceId, nullptr, nullptr, &error);
	checkError(error);

	if (!_priv::_ctx.context) {
		Log::error("Failed to create the context");
		return false;
	}
	if (!_priv::_ctx.deviceId) {
		Log::error("Failed to get the default device id");
		return false;
	}

	error = CL_SUCCESS;
	// TODO: CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE??
	const cl_queue_properties *properties = nullptr;
	/**
	 * Must be a device associated with context. It can either be in the list of devices specified when context
	 * is created using clCreateContext or have the same device type as the device type specified when the
	 * context is created using clCreateContextFromType.
	 */
	_priv::_ctx.commandQueue = clCreateCommandQueue(
			_priv::_ctx.context, _priv::_ctx.deviceId, 0, &error);
	checkError(error);

	Log::info("OpenCL Context created");
	return true;
}

void shutdown() {
	cl_int error;
	error = clReleaseCommandQueue(_priv::_ctx.commandQueue);
	checkError(error);
	error = clReleaseContext(_priv::_ctx.context);
	checkError(error);
	_priv::_ctx = _priv::Context();
}

}
