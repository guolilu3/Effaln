

#include "utils/string_padded.h"
#include "system/mm_allocator.h"


void strings_padded_add_padding(
    const char* const buffer, //pattern
    const int buffer_length, //4
    const int begin_padding_length, //18
    const int end_padding_length, //22
    const char padding_value, //'X'
    char** const buffer_padded, //pattern_padded_buffer. 完整的，18'X'+ pattern +22'X'
    char** const buffer_padded_begin, //pattern_padded. 后面用的，pattern+ 22'X' / text+ 18'Y'
    mm_allocator_t* const mm_allocator) {

  // Allocate. 44 =18+4+22
  const int buffer_padded_length = begin_padding_length + buffer_length + end_padding_length; //总长44
  *buffer_padded = mm_allocator_malloc(mm_allocator,buffer_padded_length);

  // Add begin padding. 填充左端：起点地址，写入字符，长度
  memset(*buffer_padded,padding_value,begin_padding_length); //buffer_padded 地址为开头，向后写18个'X'
  
  // Copy buffer
  *buffer_padded_begin = *buffer_padded + begin_padding_length; //跳过18后，开始插入pattern
  memcpy(*buffer_padded_begin,buffer,buffer_length); //把pattern/text拷贝过来

  // Add end padding. 填充右端：起点地址，写入字符，长度
  memset(*buffer_padded_begin+buffer_length,padding_value,end_padding_length); //写入buffer后，接着向后写22个'X'

  //pattern_padded_buffer:  XXXXXXXXXXXXXXXXXXacgtXXXXXXXXXXXXXXXXXXXXXX    8'X'+ pattern +22'X'
  //pattern_padded:                           acgtXXXXXXXXXXXXXXXXXXXXXX
  //text_padded_buffer:  YYYYYYYYYYttacgtaaYYYYYYYYYYYYYYYYYY    10'Y'+ text +18'Y'
  //text_padded:                   ttacgtaaYYYYYYYYYYYYYYYYYY
  //fprintf(stderr,"...pattern_padded_buffer:  %s\n",*buffer_padded); //取char数组的首地址
  //fprintf(stderr,"...pattern_padded:  %s\n",*buffer_padded_begin);
}


strings_padded_t* strings_padded_new(
    const char* const pattern,
    const int pattern_length,
    const char* const text,
    const int text_length,
    const int padding_length,
    mm_allocator_t* const mm_allocator) {
  // Allocate
  strings_padded_t* const strings_padded =
      mm_allocator_alloc(mm_allocator,strings_padded_t);
  strings_padded->mm_allocator = mm_allocator;
  // Compute padding dimensions
  const int pattern_begin_padding_length = 0;
  const int pattern_end_padding_length = padding_length;
  const int text_begin_padding_length = 0;
  const int text_end_padding_length = padding_length;
  // Add padding
  strings_padded_add_padding(
      pattern,pattern_length,
      pattern_begin_padding_length,pattern_end_padding_length,'X',
      &(strings_padded->pattern_padded_buffer),
      &(strings_padded->pattern_padded),mm_allocator);
  strings_padded_add_padding(
      text,text_length,
      text_begin_padding_length,text_end_padding_length,'Y',
      &(strings_padded->text_padded_buffer),
      &(strings_padded->text_padded),mm_allocator);
  // Return
  return strings_padded;
}


strings_padded_t* strings_padded_new_rhomb(
    const char* const pattern,
    const int pattern_length,
    const char* const text,
    const int text_length,
    const int padding_length, //传入. 10
    mm_allocator_t* const mm_allocator) {

  // Allocate
  strings_padded_t* const strings_padded =
      mm_allocator_alloc(mm_allocator,strings_padded_t); //声明指针
  strings_padded->mm_allocator = mm_allocator;

  // Compute padding dimensions. 填充的尺寸
  //pattern开头的填充长度
  const int pattern_begin_padding_length = text_length + padding_length; //18 =8+10
  //pattern结尾的填充长度
  const int pattern_end_padding_length = pattern_length + text_length + padding_length; //22 =4+8+10
  
  //text开头的填充长度
  const int text_begin_padding_length = padding_length; //10
  //text结尾的填充长度
  const int text_end_padding_length = text_length + padding_length; //18 =8+10

  // Add padding. 调用memset和memcpy进行设置
  strings_padded_add_padding( //pattern填充'X'
      pattern,pattern_length, //4
      pattern_begin_padding_length,pattern_end_padding_length,'X', //18，22
      &(strings_padded->pattern_padded_buffer),
      &(strings_padded->pattern_padded),mm_allocator);
  strings_padded_add_padding( //text填充'Y'
      text,text_length,
      text_begin_padding_length,text_end_padding_length,'Y',
      &(strings_padded->text_padded_buffer),
      &(strings_padded->text_padded),mm_allocator);

  // Return
  return strings_padded; //返回指针
}


void strings_padded_delete(strings_padded_t* const strings_padded) {
  mm_allocator_free(strings_padded->mm_allocator,strings_padded->pattern_padded_buffer);
  mm_allocator_free(strings_padded->mm_allocator,strings_padded->text_padded_buffer);
  mm_allocator_free(strings_padded->mm_allocator,strings_padded);
}
