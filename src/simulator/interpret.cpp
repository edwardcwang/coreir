#include "interpret.hpp"

#include "sim.hpp"

using namespace std;

namespace CoreIR {

  void SimMemory::setAddr(const BitVec& bv, const BitVec& val) {
    values.erase(bv);
    values.insert({bv, val});
  }

  BitVec SimMemory::getAddr(const BitVec& bv) const {
    auto it = values.find(bv);

    if (it == std::end(values)) {
      cout << "Could not find " << bv << endl;
      assert(false);
      //return BitVec(1, 0);
    }

    return it->second;
  }
  
  ClockValue* toClock(SimValue* val) {
    assert(val->getType() == SIM_VALUE_CLK);

    return static_cast<ClockValue*>(val);
  }

  
  void SimulatorState::setMemoryDefaults() {
    // Set constants
    for (auto& vd : gr.getVerts()) {
      WireNode wd = gr.getNode(vd);

      if (isMemoryInstance(wd.getWire())) {
	Instance* inst = toInstance(wd.getWire());
	cout << "Found memory = " << inst->toString() << endl;

	// Set memory state to default value
	SimMemory freshMem;
	memories[inst->toString()] = freshMem;

	// TODO: Actually set argument parameters
	uint width = 16;

	// Set memory output port to default
	valMap[inst->sel("rdata")] = new BitVector(BitVec(width, 0));
	
      }
    }
  }

  void SimulatorState::setConstantDefaults() {

    // Set constants
    for (auto& vd : gr.getVerts()) {
      WireNode wd = gr.getNode(vd);

      if (isInstance(wd.getWire())) {

	Instance* inst = toInstance(wd.getWire());
	string opName = getOpName(*inst);

	if (opName == "const") {
	  bool foundValue = false;

	  int argInt = 0;
	  for (auto& arg : inst->getConfigArgs()) {
	    if (arg.first == "value") {
	      foundValue = true;
	      Arg* valArg = arg.second.get(); //.get();

	      assert(valArg->getKind() == AINT);

	      ArgInt* valInt = static_cast<ArgInt*>(valArg);
	      argInt = valInt->get();
	    }
	  }

	  assert(foundValue);


	  auto outSelects = getOutputSelects(inst);

	  assert(outSelects.size() == 1);

	  pair<string, Wireable*> outPair = *std::begin(outSelects);

	  Select* outSel = toSelect(outPair.second);
	  ArrayType& arrTp = toArray(*(outSel->getType()));

	  valMap[outSel] = new BitVector(BitVec(arrTp.getLen(), argInt));
	}
      }
    }

  }

  SimulatorState::SimulatorState(CoreIR::Module* mod_) : mod(mod_) {
    buildOrderedGraph(mod, gr);
    topoOrder = topologicalSort(gr);

    // Set initial state of the circuit
    setConstantDefaults();
    setMemoryDefaults();
  }

  void SimulatorState::setValue(CoreIR::Select* sel, const BitVec& bv) {
    BitVector* b = new BitVector(bv);
    valMap[sel] = b;
  }

  
  void SimulatorState::setValue(const std::string& name, const BitVec& bv) {
    ModuleDef* def = mod->getDef();
    Wireable* w = def->sel(name);
    Select* s = toSelect(w);

    setValue(s, bv);
  }

  // NOTE: Actually implement by checking types
  bool isBitVector(SimValue* v) {
    return true;
  }

  BitVec SimulatorState::getBitVec(const std::string& str) {
    ModuleDef* def = mod->getDef();
    Wireable* w = def->sel(str);
    Select* sel = toSelect(w);

    return getBitVec(sel);
  }

  SimValue* SimulatorState::getValue(const std::string& name) {
    ModuleDef* def = mod->getDef();
    Wireable* w = def->sel(name);
    Select* sel = toSelect(w);

    return getValue(sel);
  }

  BitVec SimulatorState::getBitVec(CoreIR::Select* sel) {

    SimValue* v = getValue(sel);

    assert(isBitVector(v));

    return static_cast<BitVector*>(v)->getBits();
  }

  SimValue* SimulatorState::getValue(CoreIR::Select* sel) {
    auto it = valMap.find(sel);

    if (it == std::end(valMap)) {
      cout << "Could not find select = " << sel->toString() << endl;
      assert(false);
    }

    return (*it).second;
  }

  void SimulatorState::updateAndNode(const vdisc vd) {
    WireNode wd = gr.getNode(vd);

    Instance* inst = toInstance(wd.getWire());

    auto outSelects = getOutputSelects(inst);

    assert(outSelects.size() == 1);

    pair<string, Wireable*> outPair = *std::begin(outSelects);

    auto inConns = getInputConnections(vd, gr);

    assert(inConns.size() == 2);

    InstanceValue arg1 = findArg("in0", inConns);
    InstanceValue arg2 = findArg("in1", inConns);

    BitVector* s1 = static_cast<BitVector*>(valMap[arg1.getWire()]);
    BitVector* s2 = static_cast<BitVector*>(valMap[arg2.getWire()]);

    assert(s1 != nullptr);
    assert(s2 != nullptr);
    
    BitVec sum = s1->getBits() & s2->getBits();

    SimValue* oldVal = valMap[toSelect(outPair.second)];
    delete oldVal;

    valMap[toSelect(outPair.second)] = new BitVector(sum);
  }

  void SimulatorState::updateAddNode(const vdisc vd) {
    WireNode wd = gr.getNode(vd);

    Instance* inst = toInstance(wd.getWire());

    auto outSelects = getOutputSelects(inst);

    assert(outSelects.size() == 1);

    pair<string, Wireable*> outPair = *std::begin(outSelects);

    auto inConns = getInputConnections(vd, gr);

    assert(inConns.size() == 2);

    InstanceValue arg1 = findArg("in0", inConns);
    InstanceValue arg2 = findArg("in1", inConns);

    BitVector* s1 = static_cast<BitVector*>(valMap[arg1.getWire()]);
    BitVector* s2 = static_cast<BitVector*>(valMap[arg2.getWire()]);

    assert(s1 != nullptr);
    assert(s2 != nullptr);

    BitVec sum = add_general_width_bv(s1->getBits(), s2->getBits());

    SimValue* oldVal = valMap[toSelect(outPair.second)];
    delete oldVal;

    valMap[toSelect(outPair.second)] = new BitVector(sum);
  }

  void SimulatorState::updateMuxNode(const vdisc vd) {
    WireNode wd = gr.getNode(vd);

    Instance* inst = toInstance(wd.getWire());

    auto outSelects = getOutputSelects(inst);

    assert(outSelects.size() == 1);

    pair<string, Wireable*> outPair = *std::begin(outSelects);

    auto inConns = getInputConnections(vd, gr);

    assert(inConns.size() == 3);

    InstanceValue arg1 = findArg("in0", inConns);
    InstanceValue arg2 = findArg("in1", inConns);
    InstanceValue sel = findArg("sel", inConns);

    BitVector* s1 = static_cast<BitVector*>(valMap[arg1.getWire()]);
    BitVector* s2 = static_cast<BitVector*>(valMap[arg2.getWire()]);
    BitVector* selB = static_cast<BitVector*>(valMap[sel.getWire()]);

    assert(s1 != nullptr);
    assert(s2 != nullptr);
    assert(selB != nullptr);

    
    BitVec sum(s1->getBits().bitLength());
    
    if (selB->getBits() == BitVec(1, 0)) {
      sum = s1->getBits();
    } else {
      sum = s2->getBits();
    }

    SimValue* oldVal = valMap[toSelect(outPair.second)];
    // Is this delete always safe?
    delete oldVal;

    valMap[toSelect(outPair.second)] = new BitVector(sum);

  }

  void SimulatorState::updateOrNode(const vdisc vd) {
    WireNode wd = gr.getNode(vd);

    Instance* inst = toInstance(wd.getWire());

    auto outSelects = getOutputSelects(inst);

    assert(outSelects.size() == 1);

    pair<string, Wireable*> outPair = *std::begin(outSelects);

    auto inConns = getInputConnections(vd, gr);

    assert(inConns.size() == 2);

    InstanceValue arg1 = findArg("in0", inConns);
    InstanceValue arg2 = findArg("in1", inConns);

    BitVector* s1 = static_cast<BitVector*>(valMap[arg1.getWire()]);
    BitVector* s2 = static_cast<BitVector*>(valMap[arg2.getWire()]);

    assert(s1 != nullptr);
    assert(s2 != nullptr);
    
    BitVec sum = s1->getBits() | s2->getBits();

    SimValue* oldVal = valMap[toSelect(outPair.second)];
    // Is this delete always safe?
    delete oldVal;

    valMap[toSelect(outPair.second)] = new BitVector(sum);
  }
  
  void SimulatorState::updateOutput(const vdisc vd) {
    WireNode wd = gr.getNode(vd);

    Select* inst = toSelect(wd.getWire());

    auto outSelects = getOutputSelects(inst);

    assert(outSelects.size() == 0);

    auto inConns = getInputConnections(vd, gr);

    assert(inConns.size() == 1);

    Conn inConn = *std::begin(inConns);
    InstanceValue arg = inConn.first;

    BitVector* s = static_cast<BitVector*>(valMap[arg.getWire()]);

    assert(s != nullptr);

    SimValue* oldVal = valMap[inst];
    delete oldVal;

    valMap[inst] = new BitVector(s->getBits());
    
  }

  void SimulatorState::updateNodeValues(const vdisc vd) {
    WireNode wd = gr.getNode(vd);

    if (isGraphInput(wd)) {
      return;
    }

    if (isGraphOutput(wd)) {
      updateOutput(vd);
      return;
    }

    assert(isInstance(wd.getWire()));

    string opName = getOpName(*toInstance(wd.getWire()));
    if (opName == "and") {
      updateAndNode(vd);
      return;
    } else if (opName == "or") {
      updateOrNode(vd);
      return;
    } else if (opName == "add") {
      updateAddNode(vd);
      return;
    } else if (opName == "const") {
      return;
    } else if (opName == "reg") {
      return;
    } else if (opName == "mem") {
      return;
    } else if (opName == "mux") {
      updateMuxNode(vd);
      return;
    }

    cout << "Unsupported node: " << wd.getWire()->toString() << " has operation name: " << opName << endl;
    assert(false);
  }

  void SimulatorState::updateMemoryOutput(const vdisc vd) {
    WireNode wd = gr.getNode(vd);

    Instance* inst = toInstance(wd.getWire());

    cout << "Updating memory " << inst->toString() << endl;

    auto outSelects = getOutputSelects(inst);

    assert(outSelects.size() == 1);

    pair<string, Wireable*> outPair = *std::begin(outSelects);

    auto inConns = getInputConnections(vd, gr);

    assert(inConns.size() == 1);

    InstanceValue raddrV = findArg("raddr", inConns);

    BitVector* raddr = static_cast<BitVector*>(valMap[raddrV.getWire()]);

    assert(raddr != nullptr);

    BitVec raddrBits = raddr->getBits();

    SimValue* oldVal = valMap[toSelect(outPair.second)];
    delete oldVal;

    BitVec newRData = getMemory(inst->toString(), raddrBits);
    cout << "rdata is now value at addr " << raddrBits << " = " << newRData << endl;

    valMap[toSelect(outPair.second)] =
      //new BitVector(BitVec(23, 45564));
      new BitVector(newRData);
    
  }

  void SimulatorState::updateMemoryValue(const vdisc vd) {
    WireNode wd = gr.getNode(vd);

    Instance* inst = toInstance(wd.getWire());

    cout << "Updating memory " << inst->toString() << endl;

    auto inConns = getInputConnections(vd, gr);

    assert(inConns.size() == 4);

    InstanceValue waddrV = findArg("waddr", inConns);
    InstanceValue wdataV = findArg("wdata", inConns);
    InstanceValue clkArg = findArg("clk", inConns);
    InstanceValue enArg = findArg("wen", inConns);


    BitVector* waddr = static_cast<BitVector*>(valMap[waddrV.getWire()]);
    BitVector* wdata = static_cast<BitVector*>(valMap[wdataV.getWire()]);
    BitVector* wen = static_cast<BitVector*>(valMap[enArg.getWire()]);
    ClockValue* clkVal = toClock(valMap[clkArg.getWire()]);

    assert(waddr != nullptr);
    assert(wdata != nullptr);
    assert(wen != nullptr);
    assert(clkVal != nullptr);

    BitVec waddrBits = waddr->getBits();
    BitVec enBit = wen->getBits();

    if ((clkVal->lastValue() == 0) &&
    	(clkVal->value() == 1) &&
	(enBit == BitVec(1, 1))) {
      cout << "Setting location " << waddrBits << " to value " << wdata->getBits() << endl;
      setMemory(inst->toString(), waddrBits, wdata->getBits());

      assert(getMemory(inst->toString(), waddrBits) == wdata->getBits());
    }

  }

  BitVec SimulatorState::getMemory(const std::string& name,
				   const BitVec& addr) {
    return memories[name].getAddr(addr);
  }

  void SimulatorState::updateRegisterValue(const vdisc vd) {
    WireNode wd = gr.getNode(vd);

    Instance* inst = toInstance(wd.getWire());

    cout << "Updating register " << inst->toString() << endl;

    auto outSelects = getOutputSelects(inst);

    assert(outSelects.size() == 1);

    pair<string, Wireable*> outPair = *std::begin(outSelects);

    auto inConns = getInputConnections(vd, gr);

    assert(inConns.size() >= 2);

    InstanceValue arg1 = findArg("in", inConns);
    InstanceValue clkArg = findArg("clk", inConns);

    BitVector* s1 = static_cast<BitVector*>(valMap[arg1.getWire()]);
    ClockValue* clkVal = toClock(valMap[clkArg.getWire()]);

    assert(s1 != nullptr);
    assert(clkVal != nullptr);

    if ((clkVal->lastValue() == 0) &&
	(clkVal->value() == 1)) {

      cout << "Clock set correctly" << endl;

      if (inConns.size() == 2) {
	SimValue* oldVal = valMap[toSelect(outPair.second)];
	delete oldVal;

	valMap[toSelect(outPair.second)] = new BitVector(s1->getBits());
      } else {
	assert(inConns.size() == 3);

	InstanceValue enArg = findArg("en", inConns);	

	BitVector* enBit = static_cast<BitVector*>(valMap[enArg.getWire()]);

	assert(enBit != nullptr);

	if (enBit->getBits() == BitVec(1, 1)) {
	  SimValue* oldVal = valMap[toSelect(outPair.second)];
	  delete oldVal;

	  valMap[toSelect(outPair.second)] = new BitVector(s1->getBits());
	}
	
	// cout << "# of input connections = " << inConns.size() << endl;
	// assert(false);
      }
    }

  }

  void SimulatorState::execute() {
    for (auto& vd : topoOrder) {
      updateNodeValues(vd);
    }

    for (auto& vd : topoOrder) {
      WireNode wd = gr.getNode(vd);

      if (isMemoryInstance(wd.getWire()) && !wd.isReceiver) {
	updateMemoryOutput(vd);
      }

    }
    
    // Update circuit state
    for (auto& vd : topoOrder) {
      WireNode wd = gr.getNode(vd);
      if (isRegisterInstance(wd.getWire()) && wd.isReceiver) {
	updateRegisterValue(vd);
      }

      if (isMemoryInstance(wd.getWire())) {
	if (wd.isReceiver) {
	  updateMemoryValue(vd);
	}
      }

    }
  }

  void SimulatorState::setClock(const std::string& name,
				const unsigned char clk_last,
				const unsigned char clk) {

    ModuleDef* def = mod->getDef();
    Wireable* w = def->sel(name);
    Select* s = toSelect(w);

    setClock(s, clk_last, clk);

  }
  
  void SimulatorState::setClock(CoreIR::Select* sel,
				const unsigned char clkLast,
				const unsigned char clk) {
    SimValue* lv = valMap[sel];
    delete lv;

    valMap[sel] = new ClockValue(clkLast, clk);
  }

  void SimulatorState::setMemory(const std::string& name,
				 const BitVec& addr,
				 const BitVec& data) {

    cout << "Before seting " << name << "[ " << addr << " ] to " << data << endl;
    for (auto& memPair : memories[name]) {
      cout << "addr = " << memPair.first << " -> " << memPair.second << endl;
    }

    //memories[name].setAddr(addr, data);
    SimMemory& mem = (memories.find(name))->second;
    mem.setAddr(addr, data);
    //mem.setAddr(BitVec(12, 234), BitVec(16, 32423542));

    cout << "After set" << endl;

    for (auto& memPair : memories[name]) {
      cout << "addr = " << memPair.first << " -> " << memPair.second << endl;
    }
  }
  
  SimulatorState::~SimulatorState() {
    for (auto& val : valMap) {
      delete val.second;
    }
  }

}
