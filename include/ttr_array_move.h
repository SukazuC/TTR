#pragma once
#include <cstddef>
#include <cstring>
#include <span>
namespace ttr {inline bool MovePointer(std::span<void*>items,std::size_t from,std::size_t to)noexcept{if(from>=items.size()||to>=items.size())return false;if(from==to)return true;void*value=items[from];if(from<to)std::memmove(items.data()+from,items.data()+from+1,(to-from)*sizeof(void*));else std::memmove(items.data()+to+1,items.data()+to,(from-to)*sizeof(void*));items[to]=value;return true;}}
