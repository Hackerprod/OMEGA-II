using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Diagnostics;
class Dummy {
  [DllImport("kernel32.dll")] static extern IntPtr GetCurrentThread();
  [DllImport("kernel32.dll", SetLastError=true)] static extern UIntPtr SetThreadAffinityMask(IntPtr hThread, UIntPtr dwThreadAffinityMask);
  [DllImport("kernel32.dll")] static extern bool QueryPerformanceCounter(out long value);
  [DllImport("kernel32.dll")] static extern bool QueryPerformanceFrequency(out long value);
  [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr VirtualAlloc(IntPtr address, UIntPtr size, uint allocationType, uint protect);
  [DllImport("kernel32.dll", SetLastError=true)] static extern bool VirtualFree(IntPtr address, UIntPtr size, uint freeType);
  [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate ulong ReadTsc();
  static ulong Tsc(ReadTsc read) { Thread.MemoryBarrier(); ulong v=read(); Thread.MemoryBarrier(); return v; }
  static int Main(string[] args) {
    double delay=args.Length>0?double.Parse(args[0],System.Globalization.CultureInfo.InvariantCulture):0.8;
    double duration=args.Length>1?double.Parse(args[1],System.Globalization.CultureInfo.InvariantCulture):3.5;
    long qpf; QueryPerformanceFrequency(out qpf);
    UIntPtr old=SetThreadAffinityMask(GetCurrentThread(),new UIntPtr(1));
    byte[] code={0x0f,0x01,0xf9,0x48,0xc1,0xe2,0x20,0x48,0x09,0xd0,0xc3}; // RDTSCP; RDX:EAX -> RAX
    IntPtr mem=VirtualAlloc(IntPtr.Zero,new UIntPtr((uint)code.Length),0x3000,0x40);
    if(mem==IntPtr.Zero) { Console.WriteLine("VirtualAlloc error="+Marshal.GetLastWin32Error()); return 2; }
    Marshal.Copy(code,0,mem,code.Length);
    ReadTsc read=(ReadTsc)Marshal.GetDelegateForFunctionPointer(mem,typeof(ReadTsc));
    long q0; QueryPerformanceCounter(out q0); double delayEnd=q0+delay*qpf;
    while(true){long q;QueryPerformanceCounter(out q);if(q>=delayEnd)break;Thread.SpinWait(128);}
    DateTime utcStart=DateTime.UtcNow; long qpcStart;QueryPerformanceCounter(out qpcStart);ulong tscStart=Tsc(read);
    long iterations=0; ulong accumulator=0; long qpcEnd; do { for(int k=0;k<65536;k++){ accumulator=(accumulator+((ulong)iterations+1))&0xffffffffUL; iterations++; } QueryPerformanceCounter(out qpcEnd); } while((qpcEnd-qpcStart)/(double)qpf<duration);
    ulong tscEnd=Tsc(read); DateTime utcEnd=DateTime.UtcNow;
    double elapsed=(qpcEnd-qpcStart)/(double)qpf; double tscPerIter=(tscEnd-tscStart)/(double)iterations;
    Console.WriteLine("DUMMY=TRUE"); Console.WriteLine("AffinityLP=0"); Console.WriteLine("UtcStart="+utcStart.ToString("o"));Console.WriteLine("UtcEnd="+utcEnd.ToString("o"));
    Console.WriteLine("Iterations="+iterations);Console.WriteLine("Accumulator="+accumulator);Console.WriteLine("QpcFrequency="+qpf);Console.WriteLine("QpcElapsedSeconds="+elapsed.ToString("F6",System.Globalization.CultureInfo.InvariantCulture));
    Console.WriteLine("TscStart="+tscStart);Console.WriteLine("TscEnd="+tscEnd);Console.WriteLine("TscDelta="+(tscEnd-tscStart));Console.WriteLine("TscCyclesPerIteration="+tscPerIter.ToString("F6",System.Globalization.CultureInfo.InvariantCulture));
    SetThreadAffinityMask(GetCurrentThread(),old);VirtualFree(mem,UIntPtr.Zero,0x8000);return 0;
  }
}


