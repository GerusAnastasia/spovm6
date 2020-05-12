#include <iostream>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

namespace manager {

    typedef void* mem_ptr;
    typedef mem_ptr& mem_ref;

  // give me another way to compare void* to -1
  // P.S.: sbrk returns -1 if error occurred
    void const *invalid_memory = reinterpret_cast<void *>(-1);

    mem_ptr memory_start;
    mem_ptr memory_end;

    const uint64_t swap_size = 1024;
    uint64_t swap_used;
    uint32_t swap_descriptor;

    mem_ptr swap_start;
    mem_ptr swap_end;

    struct header_t {
        uint8_t is_acquired;
        uint64_t size;
    };

    constexpr const uint64_t header_size = sizeof(header_t);

    auto init_memory() {
      memory_start = static_cast<mem_ptr>(sbrk(0));
      memory_end = static_cast<mem_ptr>(sbrk(0));

      if (memory_start == invalid_memory || memory_end == invalid_memory) {
        fprintf(stderr, "failed to initialize memory.\n");
        fprintf(stderr, "\tmemory_start: %p\n", memory_start);
        fprintf(stderr, "\tmemory_end: %p\n", memory_end);
        perror("sbrk");
        return false;
      }

      return true;
    }

    auto init_swap() {
      swap_descriptor = open("swap", O_RDWR | O_CREAT | O_TRUNC, 0755);
      lseek(swap_descriptor, 0, SEEK_SET);
      for (uint64_t i = 0; i < swap_size; i++)
        write(swap_descriptor, "\0", 1);
      lseek(swap_descriptor, 0, SEEK_SET);
      swap_end = mmap(nullptr, swap_size, PROT_WRITE | PROT_READ, MAP_SHARED, swap_descriptor, 0);
      swap_start = swap_end;
      if (swap_end == invalid_memory) {
        perror("swap");
        return false;
      }
      return true;
    }

    void destroy() {
      close(swap_descriptor);
    }

    template<typename T>
    auto increment(T* &ptr, uint64_t delta) {
      ptr = reinterpret_cast<T*>(reinterpret_cast<uint8_t *>(ptr) + delta);
      return ptr;
    }
    void* get_block(mem_ref start, mem_ref end, uint64_t block_size) {

      for (auto *current = start; current < end; increment(current, header_size)) {
        auto header = static_cast<header_t*>(current);
        if (!header->is_acquired && header->size >= block_size) {
          header->is_acquired = true;
          return increment(current, header_size + header->size);
        }
        increment(current, header->size);
      }
      return nullptr;
    }

    void *alloc(uint64_t size, uint8_t force_swap = 0) {
      if (!force_swap) {
        if (auto *block = get_block(memory_start, memory_end, size); block != nullptr) {
          return block;
        }

        if (auto *new_end = sbrk(size + header_size); new_end != invalid_memory) {
          new(new_end) header_t {true, size};
          mem_ptr user_block = new_end;
          memory_end = increment(new_end, size + header_size);
          return increment(user_block, header_size);
        }
      }
      if (auto *block = get_block(swap_start, swap_end, size); block != nullptr) {
        return block;
      }
      if (swap_used >= swap_size) {
        fprintf(stderr, "alloc: swap is full\n");
        return nullptr;
      }

      mem_ptr new_header = swap_end;
      increment(swap_end, size + header_size);
      new (new_header) header_t { true, size };
      swap_used += header_size + size;

      return increment(new_header, header_size);
    }

    template<typename T>
    auto *malloc(uint64_t size) {
      return static_cast<T*>(alloc(size));
    }

    template<typename T>
    auto *swap_malloc(uint64_t size) {
      return static_cast<T*>(alloc(size, 1));
    }

    template<typename T>
    void free(T *memory) {
      auto *header = reinterpret_cast<header_t*>(increment(memory, -header_size));
      header->is_acquired = false;
    }

    template<typename T>
    auto *copy(T* data, const T* const_data, uint64_t size) {
      for (auto i = 0; i < size; i++)
        data[i] = const_data[i];
      return data;
    }
}
int main() {
  if (!manager::init_memory() || !manager::init_swap()) {
    return 1;
  }

  auto *ints = manager::swap_malloc<int>(3 * sizeof(int));
  ints[0] = 12;
  ints[1] = 32;
  ints[2] = 2845;

  auto *str = manager::swap_malloc<char>(16 * sizeof(char));
  manager::copy(str, "Hello, World\0", 13);

  auto *null = manager::swap_malloc<char>(1);
  fprintf(stdout, "%hhu\n", *null);
  fprintf(stdout, "%d %d %d %s\n", ints[0], ints[1], ints[2], str);

  str[0] = 'h';
  ints[1] = 2181;

  fprintf(stdout, "%d %d %d %s\n", ints[0], ints[1], ints[2], str);

  (*null) = str[2];

  fprintf(stdout, "%hhu\n", *null);

  manager::free(ints);
  manager::free(str);
  manager::free(null);

  manager::destroy();

  return 0;
}