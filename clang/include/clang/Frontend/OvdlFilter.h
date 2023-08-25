#ifndef CLANG_FRONTEND_FILTER_H
#define CLANG_FRONTEND_FILTER_H
#include <string>
namespace clang{
  struct OvdlFilterSettings{
    std::string Fname="";
    bool printEmpty=0;
    bool printNonViable=1;//Not implemented
    bool printCompares=1;//Not implemented
    size_t lineFrom=0,lineTo=(size_t)(-1);
  };
}//namespace clang

#endif
