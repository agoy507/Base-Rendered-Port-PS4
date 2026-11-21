# Bug Report & Analisis ps4_loader — STATUS: RESOLVED ✅

> **ARSIP LEGACY — BUKAN JALUR PRODUKSI.** Dokumen ini hanya menjelaskan PE loader lama. Acuan aktif adalah `Native-Rebuild-1080p60.md`, runbook GPU probe, dan source `native/`.

> **File:** `E:\PS4\hmbtn\src\ps4_loader.c` & `E:\PS4\hmbtn\build_ps4\gen_loader.js`  
> **Terakhir Diperbarui:** 10 November 2026  
> **Target:** Mod Penyu v1.3.5 (218 Imports Hooked)  

---

## 📊 Ringkasan Status Penyelesaian Bug

Seluruh potensi masalah dan bug yang teridentifikasi pada review awal kini telah **100% diperbaiki dan terverifikasi**:

| Item / Komponen | Status Awal (v1.3.0) | Status Terkini (v1.3.5) | Solusi yang Diterapkan |
| :--- | :---: | :---: | :--- |
| **`hook_longjmp` Crash** | 🔴 Kritis (`abort()`) | ✅ **RESOLVED** | Diimplementasikan menggunakan **naked x86-64 inline assembly** yang memulihkan register CPU (`RBX, RSP, RBP, RSI, RDI, R12-R15`) dan melompat ke alamat return tanpa crash. |
| **`hook__setjmp` Context** | 🟡 Tidak menyimpan state | ✅ **RESOLVED** | Diimplementasikan menggunakan **naked assembly** untuk menyimpan context stack & callee-saved registers ke buffer. |
| **DS4 Controller Mapping** | 🟡 Rawan conflict mapping | ✅ **RESOLVED** | `hook_SDL_GameControllerAddMappingsFromRW` diabaikan, memaksa engine menggunakan profil native DualShock 4 dari OpenOrbis SDL2. |
| **Win32 `CreateThread`** | ❌ Belum ada (crash) | ✅ **RESOLVED** | Emulasi handle Win32 berbasis struct magic `0x48544852` dengan wrapper MS-ABI trampoline ke POSIX thread (`pthread_create`). |
| **`WaitForSingleObject`** | 🟡 Return 0 dummy | ✅ **RESOLVED** | Menunggu flag thread `t->done` dengan timeout check berbasis microsecond. |
| **WinHTTP Updater Crash** | ❌ Belum ada (crash) | ✅ **RESOLVED** | 11 fungsi WinHTTP di-stub mengembalikan status NULL/0 aman sehingga thread updater game keluar secara bersih tanpa mencoba koneksi internet di PS4. |
| **Collision Buffer `rename()`**| 🟡 Buffer tunggal | ✅ **RESOLVED** | Menggunakan 4-slot ring buffer (`s_ring[4][512]`) sehingga pemanggilan `rename(old, new)` tidak saling menimpa path tujuan. |
| **Penyimpanan Save & Config** | 🟡 Hardcoded | ✅ **RESOLVED** | Dialihkan secara dinamis ke `/data/HarvestMoonBTN/` dengan auto-create subfolder (`mkdirs_for`). |

---

## 🔍 Detail Teknis Solusi Kritis

### 1. Naked Assembly `_setjmp` & `longjmp`
```c
__attribute__((naked)) int hook__setjmp(void* buf) {
    __asm__ volatile (
        "movq %rbx, 0(%rcx)\n\t"
        "leaq 8(%rsp), %rax\n\t"
        "movq %rax, 8(%rcx)\n\t"
        "movq %rbp, 16(%rcx)\n\t"
        "movq %rsi, 24(%rcx)\n\t"
        "movq %rdi, 32(%rcx)\n\t"
        "movq %r12, 40(%rcx)\n\t"
        "movq %r13, 48(%rcx)\n\t"
        "movq %r14, 56(%rcx)\n\t"
        "movq %r15, 64(%rcx)\n\t"
        "movq (%rsp), %rax\n\t"
        "movq %rax, 72(%rcx)\n\t"
        "xorl %eax, %eax\n\t"
        "ret\n\t"
    );
}

__attribute__((naked)) void hook_longjmp(void* env, int val) {
    __asm__ volatile (
        "movl %edx, %eax\n\t"
        "testl %eax, %eax\n\t"
        "jnz 1f\n\t"
        "movl $1, %eax\n\t"
        "1:\n\t"
        "movq 0(%rcx), %rbx\n\t"
        "movq 8(%rcx), %rsp\n\t"
        "movq 16(%rcx), %rbp\n\t"
        "movq 24(%rcx), %rsi\n\t"
        "movq 32(%rcx), %rdi\n\t"
        "movq 40(%rcx), %r12\n\t"
        "movq 48(%rcx), %r13\n\t"
        "movq 56(%rcx), %r14\n\t"
        "movq 64(%rcx), %r15\n\t"
        "movq 72(%rcx), %rdx\n\t"
        "jmpq *%rdx\n\t"
    );
}
```

### 2. Emulasi Thread Handle Win32
```c
#define HM_THREAD_MAGIC 0x48544852u
typedef struct {
    uint32_t magic;
    volatile int done;
    pthread_t th;
    void* fn;
    void* arg;
    uint32_t ret;
} hm_thread_t;

static void* hm_thread_tramp(void* p) {
    hm_thread_t* t = (hm_thread_t*)p;
    t->ret = ((ms_thread_fn_t)t->fn)(t->arg);
    t->done = 1;
    return NULL;
}
```

### 3. Proteksi DualShock 4 Exclusive
Mapping eksternal yang dimuat dari file teks diabaikan secara paksa:
```c
__attribute__((ms_abi)) int hook_SDL_GameControllerAddMappingsFromRW(SDL_RWops* rw, int freerw) {
    if (rw && freerw) SDL_RWclose(rw);
    printf("[HMBTN PS4] External controller mappings ignored (DualShock 4 native mapping only)\n");
    return 0;
}
```

---

## 📌 Kesimpulan
Seluruh subsystem `ps4_loader` telah stabil, bebas dari unhandled abort/crash conditions, dan siap untuk deployment fPKG jangka panjang.
