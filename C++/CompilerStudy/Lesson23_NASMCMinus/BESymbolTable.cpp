
#include "pch.h"
#include "BESymbolTable.h"
#include "BEType.h"

BESymbolTable::BESymbolTable(BESymbolTable *prevTable): 
    m_prevTable(prevTable), m_startOff(prevTable == NULL ? 0 : prevTable->getEndOff()), m_endOff(0), m_maxEndOff(0) {
    m_maxEndOff = m_endOff = m_startOff;
}
BESymbolTable::~BESymbolTable() {
}

BESymbolTable* BESymbolTable::getPrevTable() { 
    return m_prevTable; 
}
BESymbol* BESymbolTable::declare(const string &name, const BEType *type) {
    ASSERT(m_symbols.count(name) == 0);
    BESymbol symbol = {this, name, type, m_endOff};
    m_endOff += symbol.type->size;
    m_maxEndOff = max(m_maxEndOff, m_endOff);
    return &(m_symbols[name] = symbol);
}
void BESymbolTable::undeclare(const string& name) {
    auto iter = m_symbols.find(name);
    ASSERT(iter != m_symbols.end());
    ASSERT(m_endOff == iter->second.off + iter->second.type->size);
    m_endOff = iter->second.off;
    m_symbols.erase(iter);
}
BESymbol* BESymbolTable::get(const string &name) {
    auto iter = m_symbols.find(name);
    if (iter != m_symbols.end()) return &iter->second;
    if (m_prevTable != NULL) return m_prevTable->get(name);
    return NULL;
}
int BESymbolTable::getStartOff() const {
    return m_startOff;
}
int BESymbolTable::getEndOff() const {
    return m_endOff;
}
int BESymbolTable::getMaxEndOff() const {
    return m_maxEndOff;
}
vector<BESymbol*> BESymbolTable::getSymbols() {
    vector<BESymbol*> ret;
    for (auto &p : m_symbols) ret.push_back(&p.second);
    return ret;
}
