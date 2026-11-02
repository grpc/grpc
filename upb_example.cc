#include <iostream>
#include "upb/mem/arena.h"

int main() {
  upb_Arena* arena = upb_Arena_New();
  if (!arena) {
    std::cerr << "Failed to allocate upb_Arena\n";
    return 1;
  }

  void* memory = upb_Arena_Malloc(arena, 128);
  if (!memory) {
    std::cerr << "Failed to allocate memory in upb_Arena\n";
    upb_Arena_Free(arena);
    return 1;
  }

  std::cout << "Successfully created upb_Arena and allocated memory at "
            << memory << std::endl;

  upb_Arena_Free(arena);
  return 0;
}
