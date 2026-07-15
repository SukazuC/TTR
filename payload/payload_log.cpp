#include "payload_log.h"
#include "payload_state.h"
#include <Windows.h>
namespace ttr::payload {
#pragma pack(push,1)
struct Event{std::uint32_t tick;std::uint16_t id;std::uint16_t size;std::uint32_t a;std::uint32_t b;};
#pragma pack(pop)
void LogEvent(std::uint16_t id,std::uint32_t a,std::uint32_t b)noexcept{auto&s=GetState();if(!s.control||s.control->logSize<sizeof(Event))return;LONG offset=InterlockedExchangeAdd(&s.control->logWriteOffset,sizeof(Event));offset%=static_cast<LONG>(s.control->logSize);if(offset<0||offset+static_cast<LONG>(sizeof(Event))>static_cast<LONG>(s.control->logSize)){InterlockedIncrement(&s.control->logDroppedCount);return;}auto*event=reinterpret_cast<Event*>(static_cast<std::byte*>(s.view)+s.control->logOffset+offset);*event={static_cast<std::uint32_t>(GetTickCount64()),id,sizeof(Event),a,b};}
}
