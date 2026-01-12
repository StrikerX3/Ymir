#include <ymir/util/virtual_memory.hpp>

#ifdef WIN32

    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>

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

VirtualMemory::VirtualMemory(size_t size)
    : m_size(size)
    , m_internal(std::make_unique<Internal>()) {
    Map();
}

VirtualMemory::VirtualMemory(VirtualMemory &&rhs) {
    operator=(std::move(rhs));
}

VirtualMemory::~VirtualMemory() {
    Unmap();
}

void VirtualMemory::Map() {
#ifdef WIN32
    m_internal->hSection = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                              bit::extract<32, 63>(m_size), bit::extract<0, 31>(m_size), nullptr);
    m_mem = MapViewOfFile(m_internal->hSection, FILE_MAP_ALL_ACCESS, 0, 0, m_size);
#else // POSIX
    m_mem = mmap(NULL, m_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
#endif
}

void VirtualMemory::Unmap() {
#ifdef WIN32
    UnmapViewOfFile(m_mem);
    CloseHandle(m_internal->hSection);
#else // POSIX
    munmap(m_mem, m_size);
#endif
}

} // namespace util
