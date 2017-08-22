#ifndef SMTLIB2_HPP_
#define SMTLIB2_HPP_

#include "coreir.h"
#include <ostream>
#include "smtmodule.hpp"

using namespace CoreIR;
namespace CoreIR {
namespace Passes {

class SmtLib2 : public InstanceGraphPass {
  unordered_map<Instantiable*,VModule*> modMap;
  unordered_set<Instantiable*> external;
  public :
    static std::string ID;
    SmtLib2() : InstanceGraphPass(ID,"Creates SmtLib2 representation of IR",true) {}
    bool runOnInstanceGraphNode(InstanceGraphNode& node) override;
    void setAnalysisInfo() override {
      addDependency("strongverify");
      addDependency("verifyflattenedtypes");
    }
    
    void writeToStream(std::ostream& os);
};

}
}
#endif
