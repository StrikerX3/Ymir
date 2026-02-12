#include <ymir/util/virtual_memory.hpp>

#ifdef WIN32

    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>

    #include <ymir/util/bit_ops.hpp>

#else // POSIX

    #include <sys/mman.h>

#endif

namespace util {

struct VirtualMemory::Internal {
#ifdef WIN32
    HANDLE hSection;
#else // POSIX
#endif
};

VirtualMemory::VirtualMemory()
    : m_internal(std::make_unique<Internal>()) {}

VirtualMemory::VirtualMemory(size_t size)
    : m_size(size)
    , m_internal(std::make_unique<Internal>()) {
    Map(size);
}

VirtualMemory::VirtualMemory(VirtualMemory &&rhs) {
    operator=(std::move(rhs));
}

VirtualMemory::~VirtualMemory() {
    Free();
}

void *VirtualMemory::Allocate(size_t size) {
    Free();
    Map(size);
    return m_mem;
}

void VirtualMemory::Free() {
    if (m_mem != nullptr) {
        Unmap();
        m_mem = nullptr;
    }
}

void VirtualMemory::Map(size_t size) {
#ifdef WIN32
    m_internal->hSection = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, bit::extract<32, 63>(size),
                                              bit::extract<0, 31>(size), nullptr);
    m_mem = MapViewOfFile(m_internal->hSection, FILE_MAP_ALL_ACCESS, 0, 0, size);
#else // POSIX
    m_mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
#endif
    m_size = size;
}

void VirtualMemory::Unmap() {
#ifdef WIN32
    UnmapViewOfFile(m_mem);
    CloseHandle(m_internal->hSection);
    m_internal->hSection = INVALID_HANDLE_VALUE;
#else // POSIX
    munmap(m_mem, m_size);
#endif
    m_mem = nullptr;
    m_size = 0;
}

} // namespace util
