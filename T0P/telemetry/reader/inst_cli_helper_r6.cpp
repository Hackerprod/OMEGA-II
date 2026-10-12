#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>

extern "C" uint64_t known_retired_loop(uint64_t iterations);

static constexpr uint64_t N_LOOP = 200000000000ULL;
static constexpr uint64_t I0 = 3ULL * N_LOOP + 4ULL;
static constexpr unsigned WORKER_COUNT = 8;
static constexpr unsigned WORKER_LPS[WORKER_COUNT] = {0, 2, 4, 6, 8, 10, 12, 14};

struct Worker {
    unsigned lp;
    HANDLE gate;
    volatile LONG* ready;
    uint64_t result;
    uint64_t qpc_start;
    uint64_t qpc_end;
    DWORD lp_begin;
    DWORD lp_end;
    WORD group_begin;
    WORD group_end;
    bool affinity_ok;
};

struct MapState {
    const wchar_t* path;
    volatile LONG stop;
    volatile LONG ready;
    volatile LONG succeeded;
};

static LARGE_INTEGER g_qpf;

static bool write_map_row(HANDLE file, const char* tag, uint64_t sample) {
    LARGE_INTEGER q0{};
    LARGE_INTEGER q1{};
    FILETIME ft{};
    QueryPerformanceCounter(&q0);
    GetSystemTimePreciseAsFileTime(&ft);
    QueryPerformanceCounter(&q1);
    ULARGE_INTEGER utc{};
    utc.LowPart = ft.dwLowDateTime;
    utc.HighPart = ft.dwHighDateTime;
    char row[192];
    char label[32];
    if (strcmp(tag, "SAMPLE") == 0)
        _snprintf_s(label, sizeof(label), _TRUNCATE, "%llu", static_cast<unsigned long long>(sample));
    else
        strcpy_s(label, tag);
    const int size = _snprintf_s(row, sizeof(row), _TRUNCATE, "%s,%llu,%llu,%llu\r\n",
        label, static_cast<unsigned long long>(q0.QuadPart),
        static_cast<unsigned long long>(q1.QuadPart),
        static_cast<unsigned long long>(utc.QuadPart));
    if (size <= 0) return false;
    DWORD written = 0;
    return WriteFile(file, row, static_cast<DWORD>(size), &written, nullptr) != 0 &&
           written == static_cast<DWORD>(size) && FlushFileBuffers(file) != 0;
}

static DWORD WINAPI map_proc(LPVOID arg) {
    MapState* state = static_cast<MapState*>(arg);
    HANDLE file = CreateFileW(state->path, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE || !SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR(1) << 16)) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        InterlockedExchange(&state->ready, 1);
        InterlockedExchange(&state->succeeded, 0);
        return 1;
    }
    static const char header[] = "sample,qpc_pre,qpc_post,filetime_100ns\r\n";
    DWORD written = 0;
    bool ok = WriteFile(file, header, static_cast<DWORD>(sizeof(header) - 1), &written, nullptr) != 0 &&
              written == sizeof(header) - 1 && FlushFileBuffers(file) != 0 && write_map_row(file, "BEGIN", 0);
    InterlockedExchange(&state->succeeded, ok ? 1 : 0);
    InterlockedExchange(&state->ready, 1);
    uint64_t sample = 0;
    while (ok && InterlockedCompareExchange(&state->stop, 0, 0) == 0) {
        ok = write_map_row(file, "SAMPLE", sample++);
        Sleep(100);
    }
    if (ok) ok = write_map_row(file, "END", sample);
    InterlockedExchange(&state->succeeded, ok ? 1 : 0);
    CloseHandle(file);
    return ok ? 0 : 2;
}

static DWORD WINAPI worker_proc(LPVOID arg) {
    Worker* w = static_cast<Worker*>(arg);
    const DWORD_PTR previous = SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR(1) << w->lp);
    GROUP_AFFINITY effective{};
    PROCESSOR_NUMBER before{};
    w->affinity_ok = previous != 0 && GetThreadGroupAffinity(GetCurrentThread(), &effective) != 0 &&
                     effective.Group == 0 && effective.Mask == (DWORD_PTR(1) << w->lp);
    InterlockedIncrement(w->ready);
    WaitForSingleObject(w->gate, INFINITE);
    GetCurrentProcessorNumberEx(&before);
    w->group_begin = before.Group;
    w->lp_begin = before.Number;
    LARGE_INTEGER start{};
    LARGE_INTEGER end{};
    QueryPerformanceCounter(&start);
    w->result = known_retired_loop(N_LOOP);
    QueryPerformanceCounter(&end);
    PROCESSOR_NUMBER after{};
    GetCurrentProcessorNumberEx(&after);
    w->group_end = after.Group;
    w->lp_end = after.Number;
    w->qpc_start = static_cast<uint64_t>(start.QuadPart);
    w->qpc_end = static_cast<uint64_t>(end.QuadPart);
    w->affinity_ok = w->affinity_ok && before.Group == 0 && after.Group == 0 &&
                     before.Number == w->lp && after.Number == w->lp;
    return 0;
}

static bool write_new_report(const wchar_t* path, const Worker workers[WORKER_COUNT],
                             uint64_t run_start, uint64_t run_end) {
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    char buffer[4096];
    int used = _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
        "DUMMY_NATIVE=TRUE\r\nN_loop=%llu\r\nI0=%llu\r\nQPF=%lld\r\nrun_qpc_start=%llu\r\nrun_qpc_end=%llu\r\nlp,group_begin,lp_begin,group_end,lp_end,result,expected_result,expected_retired,qpc_start,qpc_end,duration_s,affinity\r\n",
        static_cast<unsigned long long>(N_LOOP), static_cast<unsigned long long>(I0),
        g_qpf.QuadPart, static_cast<unsigned long long>(run_start),
        static_cast<unsigned long long>(run_end));
    bool ok = used > 0;
    for (unsigned i = 0; ok && i < WORKER_COUNT; ++i) {
        const Worker& w = workers[i];
        const double duration = static_cast<double>(w.qpc_end - w.qpc_start) /
                                static_cast<double>(g_qpf.QuadPart);
        char row[384];
        const int row_size = _snprintf_s(row, sizeof(row), _TRUNCATE,
            "%u,%u,%lu,%u,%lu,%llu,%llu,%llu,%llu,%llu,%.9f,%s\r\n",
            w.lp, w.group_begin, static_cast<unsigned long>(w.lp_begin), w.group_end,
            static_cast<unsigned long>(w.lp_end), static_cast<unsigned long long>(w.result),
            static_cast<unsigned long long>(N_LOOP), static_cast<unsigned long long>(I0),
            static_cast<unsigned long long>(w.qpc_start), static_cast<unsigned long long>(w.qpc_end),
            duration, w.affinity_ok ? "PASS" : "FAIL");
        if (row_size <= 0 || static_cast<size_t>(used + row_size) >= sizeof(buffer)) {
            ok = false;
            break;
        }
        memcpy(buffer + used, row, static_cast<size_t>(row_size));
        used += row_size;
    }
    DWORD written = 0;
    if (ok) ok = WriteFile(file, buffer, static_cast<DWORD>(used), &written, nullptr) != 0 &&
                 written == static_cast<DWORD>(used);
    if (ok) ok = FlushFileBuffers(file) != 0;
    CloseHandle(file);
    return ok;
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        fwprintf(stderr, L"Uso: inst_cli_helper_r6.exe <reporte_nuevo.csv> <mapa_qpc_utc_nuevo.csv>\n");
        return 2;
    }
    if (GetActiveProcessorCount(0) <= 16 || !QueryPerformanceFrequency(&g_qpf) || g_qpf.QuadPart <= 0)
        return 3;
    if (!SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR(1) << 16)) return 4;
    PROCESSOR_NUMBER controller{};
    GetCurrentProcessorNumberEx(&controller);
    if (controller.Group != 0 || controller.Number != 16) return 5;

    HANDLE gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!gate) return 6;
    MapState map{argv[2], 0, 0, 0};
    HANDLE map_thread = CreateThread(nullptr, 0, map_proc, &map, 0, nullptr);
    if (!map_thread) { CloseHandle(gate); return 7; }
    while (InterlockedCompareExchange(&map.ready, 0, 0) == 0) Sleep(1);
    if (InterlockedCompareExchange(&map.succeeded, 0, 0) == 0) {
        WaitForSingleObject(map_thread, INFINITE);
        CloseHandle(map_thread);
        CloseHandle(gate);
        return 8;
    }
    volatile LONG ready = 0;
    Worker workers[WORKER_COUNT]{};
    HANDLE threads[WORKER_COUNT]{};
    bool created = true;
    for (unsigned i = 0; i < WORKER_COUNT; ++i) {
        workers[i].lp = WORKER_LPS[i];
        workers[i].gate = gate;
        workers[i].ready = &ready;
        threads[i] = CreateThread(nullptr, 0, worker_proc, &workers[i], 0, nullptr);
        if (!threads[i]) { created = false; break; }
    }
    if (!created) {
        SetEvent(gate);
        for (HANDLE thread : threads) if (thread) { WaitForSingleObject(thread, INFINITE); CloseHandle(thread); }
        InterlockedExchange(&map.stop, 1);
        WaitForSingleObject(map_thread, INFINITE);
        CloseHandle(map_thread);
        CloseHandle(gate);
        return 9;
    }
    while (InterlockedCompareExchange(&ready, 0, 0) != static_cast<LONG>(WORKER_COUNT)) Sleep(1);
    const bool all_affinity_set = [&]() {
        for (const Worker& w : workers) if (!w.affinity_ok) return false;
        return true;
    }();
    if (!all_affinity_set) {
        SetEvent(gate);
        for (HANDLE thread : threads) { WaitForSingleObject(thread, INFINITE); CloseHandle(thread); }
        InterlockedExchange(&map.stop, 1);
        WaitForSingleObject(map_thread, INFINITE);
        CloseHandle(map_thread);
        CloseHandle(gate);
        return 10;
    }
    LARGE_INTEGER run_start{};
    LARGE_INTEGER run_end{};
    QueryPerformanceCounter(&run_start);
    SetEvent(gate);
    for (HANDLE thread : threads) WaitForSingleObject(thread, INFINITE);
    QueryPerformanceCounter(&run_end);
    for (HANDLE thread : threads) CloseHandle(thread);
    InterlockedExchange(&map.stop, 1);
    WaitForSingleObject(map_thread, INFINITE);
    CloseHandle(map_thread);
    CloseHandle(gate);

    bool valid = true;
    for (const Worker& w : workers) {
        const double duration = static_cast<double>(w.qpc_end - w.qpc_start) /
                                static_cast<double>(g_qpf.QuadPart);
        valid = valid && w.result == N_LOOP && w.affinity_ok && duration >= 20.0 &&
                w.qpc_end > w.qpc_start;
    }
    const bool report_ok = InterlockedCompareExchange(&map.succeeded, 0, 0) == 1 && write_new_report(argv[1], workers,
        static_cast<uint64_t>(run_start.QuadPart), static_cast<uint64_t>(run_end.QuadPart));
    for (const Worker& w : workers) {
        const double duration = static_cast<double>(w.qpc_end - w.qpc_start) /
                                static_cast<double>(g_qpf.QuadPart);
        wprintf(L"LP=%u group=%u result=%llu expected=%llu I0=%llu duration_s=%.9f affinity=%ls\n",
            w.lp, w.group_begin, static_cast<unsigned long long>(w.result),
            static_cast<unsigned long long>(N_LOOP), static_cast<unsigned long long>(I0),
            duration, w.affinity_ok ? L"PASS" : L"FAIL");
    }
    wprintf(L"DUMMY_NATIVE=TRUE; N_loop fixed at %llu; no EMON invoked; no instruction-count measurement; report=%ls; map=%ls; create_new=%ls; status=%ls\n",
        static_cast<unsigned long long>(N_LOOP), argv[1], argv[2], report_ok ? L"PASS" : L"FAIL",
        valid && report_ok ? L"PASS" : L"FAIL");
    return valid && report_ok ? 0 : 9;
}
