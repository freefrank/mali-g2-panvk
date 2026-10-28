
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <dlfcn.h>
#include <string.h>
typedef int32_t cl_int;
typedef uint32_t cl_uint;
typedef uint64_t cl_ulong;
typedef cl_ulong cl_device_type;
typedef intptr_t cl_context_properties;
typedef void *cl_platform_id;
typedef void *cl_device_id;
typedef void *cl_context;
typedef void *cl_command_queue;
typedef void *cl_mem;
typedef void *cl_program;
typedef void *cl_kernel;
#define DECL(ret,name,args) typedef ret (*name##_t) args; name##_t name=(name##_t)dlsym(lib,#name); if(!name){printf("MISSING_SYMBOL: %s\n",#name);return 2;}
int main(int argc,char **argv){
 setbuf(stdout,NULL);
 const char *paths[]={"/vendor/lib64/libOpenCL.so","/vendor/lib64/egl/libGLES_mali.so",NULL};
 void *lib=NULL;
 for(int i=0;paths[i];i++){
  lib=dlopen(paths[i],RTLD_NOW|RTLD_LOCAL);
  if(lib){printf("OPENCL_LIBRARY: %s\n",paths[i]);break;}
  printf("DLOPEN_ERROR: %s: %s\n",paths[i],dlerror());
 }
 if(!lib)return 1;
 DECL(cl_int,clGetPlatformIDs,(cl_uint,cl_platform_id*,cl_uint*));
 DECL(cl_int,clGetPlatformInfo,(cl_platform_id,cl_uint,size_t,void*,size_t*));
 DECL(cl_int,clGetDeviceIDs,(cl_platform_id,cl_device_type,cl_uint,cl_device_id*,cl_uint*));
 DECL(cl_int,clGetDeviceInfo,(cl_device_id,cl_uint,size_t,void*,size_t*));
 cl_platform_id platforms[8]; cl_uint np=0;
 cl_int e=clGetPlatformIDs(8,platforms,&np);printf("PLATFORMS: status=%d count=%u\n",e,np);
 if(e||!np)return 3;
 cl_device_id dev=NULL;
 for(cl_uint i=0;i<np&&i<8;i++){
  char s[4096]={0};clGetPlatformInfo(platforms[i],0x0902,sizeof(s),s,NULL);printf("PLATFORM: %s\n",s);
  cl_device_id ds[16];cl_uint nd=0;e=clGetDeviceIDs(platforms[i],4,16,ds,&nd);
  printf("GPU_DEVICES: status=%d count=%u\n",e,nd);
  if(e)continue;
  for(cl_uint j=0;j<nd&&j<16;j++){
   const cl_uint fields[]={0x102B,0x102C,0x102D,0x102F,0x103D};
   const char *labels[]={"GPU_NAME","GPU_VENDOR","DRIVER_VERSION","OPENCL_VERSION","OPENCL_C_VERSION"};
   for(int k=0;k<5;k++){memset(s,0,sizeof(s));e=clGetDeviceInfo(ds[j],fields[k],sizeof(s),s,NULL);printf("%s: %s (status=%d)\n",labels[k],s,e);}
   cl_uint cu=0;clGetDeviceInfo(ds[j],0x1002,sizeof(cu),&cu,NULL);printf("COMPUTE_UNITS: %u\n",cu);
   if(!dev)dev=ds[j];
  }
 }
 if(!dev)return 4;
 DECL(cl_context,clCreateContext,(const cl_context_properties*,cl_uint,const cl_device_id*,void(*)(const char*,const void*,size_t,void*),void*,cl_int*));
 DECL(cl_command_queue,clCreateCommandQueue,(cl_context,cl_device_id,cl_ulong,cl_int*));
 DECL(cl_mem,clCreateBuffer,(cl_context,cl_ulong,size_t,void*,cl_int*));
 DECL(cl_program,clCreateProgramWithSource,(cl_context,cl_uint,const char**,const size_t*,cl_int*));
 DECL(cl_int,clBuildProgram,(cl_program,cl_uint,const cl_device_id*,const char*,void(*)(cl_program,void*),void*));
 DECL(cl_int,clGetProgramBuildInfo,(cl_program,cl_device_id,cl_uint,size_t,void*,size_t*));
 DECL(cl_kernel,clCreateKernel,(cl_program,const char*,cl_int*));
 DECL(cl_int,clSetKernelArg,(cl_kernel,cl_uint,size_t,const void*));
 DECL(cl_int,clEnqueueNDRangeKernel,(cl_command_queue,cl_kernel,cl_uint,const size_t*,const size_t*,const size_t*,cl_uint,const void*,void*));
 DECL(cl_int,clEnqueueReadBuffer,(cl_command_queue,cl_mem,cl_uint,size_t,size_t,void*,cl_uint,const void*,void*));
 DECL(cl_int,clEnqueueWriteBuffer,(cl_command_queue,cl_mem,cl_uint,size_t,size_t,const void*,cl_uint,const void*,void*));
 DECL(cl_int,clReleaseKernel,(cl_kernel));
 DECL(cl_int,clReleaseProgram,(cl_program));
 DECL(cl_int,clReleaseMemObject,(cl_mem));
 DECL(cl_int,clReleaseCommandQueue,(cl_command_queue));
 DECL(cl_int,clReleaseContext,(cl_context));
 cl_context ctx=clCreateContext(NULL,1,&dev,NULL,NULL,&e);printf("CREATE_CONTEXT: %d\n",e);if(e)return 5;
 cl_command_queue q=clCreateCommandQueue(ctx,dev,0,&e);printf("CREATE_QUEUE: %d\n",e);if(e)return 6;
 int out[4096]={0};cl_mem b=clCreateBuffer(ctx,1,sizeof(out),NULL,&e);if(e){printf("BUFFER_ERROR: %d\n",e);return 7;}
 const char *src=argc>2?argv[2]:"__kernel void probe(__global int *out) { out[0]=0x12345678; }";
 cl_program prog=clCreateProgramWithSource(ctx,1,&src,NULL,&e);if(e){printf("PROGRAM_ERROR: %d\n",e);return 8;}
 e=clBuildProgram(prog,1,&dev,"",NULL,NULL);printf("BUILD_KERNEL: %d\n",e);
 if(e){char log[8192]={0};clGetProgramBuildInfo(prog,dev,0x1183,sizeof(log),log,NULL);printf("BUILD_LOG: %s\n",log);return 9;}

 DECL(cl_int,clGetProgramInfo,(cl_program,cl_uint,size_t,void*,size_t*));
 size_t bytes=0;e=clGetProgramInfo(prog,0x1165,sizeof(bytes),&bytes,NULL);
 printf("BINARY_SIZE: %zu status=%d\n",bytes,e);
 if(e||bytes>16777216)return 20;
 unsigned char *binary=malloc(bytes);e=clGetProgramInfo(prog,0x1166,sizeof(binary),&binary,NULL);
 if(e)return 21;
 FILE *file=fopen(argc>1?argv[1]:"/root/mali-g2-backend/build/backend/constant.bin","wb");
 if(!file)return 22;
 fwrite(binary,1,bytes,file);fclose(file);free(binary);
 if(argc<4||strcmp(argv[3],"run")){
  clReleaseProgram(prog);clReleaseMemObject(b);clReleaseCommandQueue(q);clReleaseContext(ctx);
  return 0;
 }
 e=clEnqueueWriteBuffer(q,b,1,0,sizeof(out),out,0,NULL,NULL);if(e)return 23;
 cl_kernel k=clCreateKernel(prog,"probe",&e);if(e){printf("KERNEL_ERROR: %d\n",e);return 10;}
 e=clSetKernelArg(k,0,sizeof(b),&b);if(e){printf("ARG_ERROR: %d\n",e);return 11;}
 size_t n=32;e=clEnqueueNDRangeKernel(q,k,1,NULL,&n,NULL,0,NULL,NULL);printf("DISPATCH: %d\n",e);if(e)return 12;
 e=clEnqueueReadBuffer(q,b,1,0,sizeof(out),out,0,NULL,NULL);printf("READBACK: %d\n",e);if(e)return 13;
 int ok=1;for(int i=0;i<32;i++){if(out[i]!=i*i+7)ok=0;}
 printf("GPU_COMPUTE_TEST: %s (32 integers, last=%d expected=968)\n",ok?"PASS":"FAIL",out[31]);
 clReleaseKernel(k);clReleaseProgram(prog);clReleaseMemObject(b);clReleaseCommandQueue(q);clReleaseContext(ctx);
 return ok?0:14;
}
