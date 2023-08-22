#ifndef ovldPrinter_h
#define ovldPrinter_h
#include<string>
namespace ovld{
	struct Cand{
		std::string signature;
		std::string location;
	};
	struct CompareRes{
		enum Better{C1,C2,undefined};
		Better better;
		Cand C1,C2;
		std::string reason;
	};
	struct ListRes{
		std::vector<Cand> candidates;
		Cand best;
		std::string PointOfCall;
	};
	class printer{
		private:
		public:

	};
}

#endif
