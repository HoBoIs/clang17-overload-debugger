
#ifndef LLVM_CLANG_SEMA_OVERLOGGER_H
#define LLVM_CLANG_SEMA_OVERLOGGER_H

#include "clang/Sema/SemaInternal.h"
namespace overload_debug{
enum Loging_mode{
    none,all,implicit_only,fun_name
};

class OverLogger{
    Loging_mode loging=none;
    public:
    void set_loging(Loging_mode);
    Loging_mode get_logging()const;
    void log_message(const char*);
    template<class T>
    OverLogger& operator<<(const T& m){
	if (Loging_mode::all==loging) llvm::errs()<<m;
	return *this;
    }
    inline bool is_loging() const{
	return loging==all;
    }
};
extern OverLogger logger;
}
/*
#include "clang/Sema/OverLogger.h"
//TODO make it to its own file
namespace overload_debug{

void OverLogger::set_loging(Loging_mode lm){
    loging=lm;
};
void OverLogger::log_message(const char* message){
    switch (loging){
	case all:
	llvm::errs()<<message;
	break;
	case none:
	break;
	case implicit_only:
	break;//TODO ()
	case fun_name:
	break;//TODO
    }
}
Loging_mode OverLogger::get_logging()const{
    return loging;
}
    OverLogger logger;
}*/
#endif
