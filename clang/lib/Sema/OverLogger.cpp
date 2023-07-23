#include "clang/Sema/OverLogger.h"

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
}
