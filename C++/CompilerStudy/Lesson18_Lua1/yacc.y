%token CONTINUE BREAK DO ELSE ELSEIF END FALSE 
%token FOR FUNCTION IF IN LOCAL NIL
%token REPEAT RETURN THEN TRUE UNTIL WHILE

%token OP_ARGS

%token LITERAL1 LITERAL2 LITERAL3
%token NUMBER1 NUMBER2 NUMBER3

%token ID

%right OP_ASSIGN

%left AND OR
%left OP_EQUAL OP_NEQUAL OP_LEQUAL OP_GEQUAL OP_LESS OP_GREATER
%left OP_ADD OP_SUB
%left OP_MUL OP_DIV OP_MOD
%left OP_POWER
%left OP_CONCAT

%nonassoc OP_LEN NOT

%{

#include "LuaFunction.h"
#include "SymbolTable.h"

static ExpNodePtr getLocalExp(const string& name);
static ExpNodePtr getExpByIDName(const string& name);
static ExpNodePtr getConstExp(const LuaValue& v);
static string unEscape(const string& s);
static bool& isDefiningMethod();
static string& srcFileName();
static int& srcFileLine();
extern int getTokenLine();

#define FUNC_META (LuaFunctionMeta::stack()->top().get())

%}

%%

Program : Block {
        LuaFunctionMeta::stack()->top()->body = $1.get<StmtNodePtr>();
        }
        ;

Block
    : StatementList {
        int blockOff = SymbolTable::top()->getBlockOff(), blockSize = SymbolTable::top()->getBlockSize();
        $$ = StmtNodePtr(new BlockStmtNode(blockOff, blockSize, $1.get<vector<StmtNodePtr> >()));
    }
    | LastStatement {
        auto vec = vector<StmtNodePtr>();
        vec.push_back($1.get<StmtNodePtr>());
        int blockOff = SymbolTable::top()->getBlockOff(), blockSize = SymbolTable::top()->getBlockSize();
        $$ = StmtNodePtr(new BlockStmtNode(blockOff, blockSize, vec));
    }
    | StatementList StatementSep LastStatement {
        auto &vec = $1.get<vector<StmtNodePtr> >();
        vec.push_back($3.get<StmtNodePtr>());
        int blockOff = SymbolTable::top()->getBlockOff(), blockSize = SymbolTable::top()->getBlockSize();
        $$ = StmtNodePtr(new BlockStmtNode(blockOff, blockSize, vec));
    }
    | {
        int blockOff = SymbolTable::top()->getBlockOff(), blockSize = SymbolTable::top()->getBlockSize();
        $$ = StmtNodePtr(new BlockStmtNode(blockOff, blockSize, vector<StmtNodePtr>()));
    }
    ;

StatementList
    : Statement {
        auto vec = vector<StmtNodePtr>();
        vec.push_back($1.get<StmtNodePtr>());
        $$ = vec;
    }
    | StatementList StatementSep Statement {
        $1.get<vector<StmtNodePtr> >().push_back($3.get<StmtNodePtr>());
        $$ = move($1);
    }
    ;

StatementSep : ';' | ;

Statement
    : VarList OP_ASSIGN ExpList {
        $$ = StmtNodePtr(new AssignStmtNode($1.get<vector<ExpNodePtr> >(), $3.get<vector<ExpNodePtr> >()));
    }

    | FunctionCall  {
        $$ = StmtNodePtr(new CallStmtNode($1.get<ExpNodePtr>()));
    }

    | DO {
        SymbolTable::top()->beginBlock();
    } Block END {
        SymbolTable::top()->endBlock();
        $$ = $3.get<StmtNodePtr>();
    }

    | WHILE Exp {
        SymbolTable::top()->beginBlock();
    } DO Block END {
        SymbolTable::top()->endBlock();
        $$ = StmtNodePtr(new LoopForStmtNode(StmtNodePtr(), $2.get<ExpNodePtr>(), $5.get<StmtNodePtr>()));
    }

    | REPEAT {
        SymbolTable::top()->beginBlock();
    } Block UNTIL Exp {
        SymbolTable::top()->endBlock();
        auto &block = $3.get<StmtNodePtr>();
        auto pred = ExpNodePtr(new UnOpExpNode(UOP_Not, $5.get<ExpNodePtr>()));
        $$ = StmtNodePtr(new LoopForStmtNode(block, pred, block));
    }

    | IF Exp THEN {
        SymbolTable::top()->beginBlock();
    } Block {
        SymbolTable::top()->endBlock();
    } Opt_ElseIfList Opt_ElseBlock END {
        auto ifStmt = new IfElseStmtNode();
        ifStmt->ifExpStmtList = move($7.get<vector<pair<ExpNodePtr, StmtNodePtr> > >());
        ifStmt->ifExpStmtList.insert(ifStmt->ifExpStmtList.end(),
            make_pair($2.get<ExpNodePtr>(), $5.get<StmtNodePtr>()));
        ifStmt->elseStmt = $8.get<StmtNodePtr>();
        $$ = StmtNodePtr(ifStmt);
    }

    | FOR ID OP_ASSIGN Exp ',' Exp {
        SymbolTable::top()->beginBlock();
        auto forStmt = new RangeForStmtNode(FUNC_META, $2.get<string>());
        $1 = StmtNodePtr(forStmt);
    } DO Block END {
        SymbolTable::top()->endBlock();
        auto forStmt = static_cast<RangeForStmtNode*>($1.get<StmtNodePtr>().get());
        $$ = move($1);
        forStmt->first = $4.get<ExpNodePtr>(); 
        forStmt->last = $6.get<ExpNodePtr>(); 
        forStmt->step = getConstExp(LuaValue(NumberType(1)));
        forStmt->stmt = $9.get<StmtNodePtr>();
    }

    | FOR ID OP_ASSIGN Exp ',' Exp ',' Exp {
        SymbolTable::top()->beginBlock();
        auto forStmt = new RangeForStmtNode(FUNC_META, $2.get<string>());
        $1 = StmtNodePtr(forStmt);
    } DO Block END {
        SymbolTable::top()->endBlock();
        auto forStmt = static_cast<RangeForStmtNode*>($1.get<StmtNodePtr>().get());
        $$ = move($1);
        forStmt->first = $4.get<ExpNodePtr>(); 
        forStmt->last = $6.get<ExpNodePtr>(); 
        forStmt->step = $8.get<ExpNodePtr>(); 
        forStmt->stmt = $11.get<StmtNodePtr>();
    }

    | FOR IDList IN ExpList {
        SymbolTable::top()->beginBlock();
        auto forStmt = new IteraterForStmtNode();
        forStmt->iterExps = $4.get<vector<ExpNodePtr> >();
        for (auto &name : $2.get<vector<string> >()) {
            forStmt->pushName(FUNC_META, name);
        }
        $1 = StmtNodePtr(forStmt);
    }
    DO Block END {
        SymbolTable::top()->endBlock();
        auto forStmt = static_cast<IteraterForStmtNode*>($1.get<StmtNodePtr>().get());
        $$ = move($1);
        forStmt->stmt = $7.get<StmtNodePtr>();
    }

    | FUNCTION FuncName FuncBody  {
        auto &names = $2.get<vector<string> >();
        auto vars = vector<ExpNodePtr>();
        auto exps = vector<ExpNodePtr>();
        ExpNodePtr funcExp = getExpByIDName(names[0]);
        if (names.size() > 1) {
            for (int i = 1; i < (int)names.size(); ++i) { 
                funcExp = ExpNodePtr(new FieldAccessExpNode(funcExp, getConstExp(LuaValue(names[i].c_str()))));
            }
        }
        vars.push_back(funcExp);
        exps.push_back($3.get<ExpNodePtr>());
        $$ = StmtNodePtr(new AssignStmtNode(vars, exps));
    }
    | LOCAL FUNCTION ID {
        string name = $3.get<string>();
        SymbolTable::top()->declareLocal(name);
    } FuncBody {
        string name = $3.get<string>();
        auto vars = vector<ExpNodePtr>();
        auto exps = vector<ExpNodePtr>();
        vars.push_back(getLocalExp(name));
        exps.push_back($5.get<ExpNodePtr>());
        $$ = StmtNodePtr(new AssignStmtNode(vars, exps));
    }
    | LOCAL IDList {
        for (auto &name : $2.get<vector<string> >()) {
            SymbolTable::top()->declareLocal(name);
        }
        $$ = StmtNodePtr();
    }
    | LOCAL IDList OP_ASSIGN ExpList {
        vector<ExpNodePtr> vars;
        for (auto &name : $2.get<vector<string> >()) {
            SymbolTable::top()->declareLocal(name);
            vars.push_back(getLocalExp(name));
        }
        $$ = StmtNodePtr(new AssignStmtNode(vars, $4.get<vector<ExpNodePtr> >()));
    }
    ;

Opt_ElseBlock
    : ElseBlock 
    | {
        $$ = StmtNodePtr();
    }
    ;
ElseBlock: ELSE Block {
         $$ = move($2);
         }
         ;

Opt_ElseIfList 
    : ElseIfList
    | {
        $$ = vector<pair<ExpNodePtr, StmtNodePtr> >();
    }
    ;

ElseIfList 
    : ElseIf {
        auto vec = vector<pair<ExpNodePtr, StmtNodePtr> >();
        vec.push_back($1.get<pair<ExpNodePtr, StmtNodePtr> >());
        $$ = vec;
    }
    | ElseIfList ElseIf {
        auto &vec = $1.get<vector<pair<ExpNodePtr, StmtNodePtr> > >();
        vec.push_back($2.get<pair<ExpNodePtr, StmtNodePtr> >());
        $$ = move($1);
    }
    ;
ElseIf : ELSEIF Exp {
        SymbolTable::top()->beginBlock();
       } THEN Block {
        SymbolTable::top()->endBlock();
        $$ = make_pair($2.get<ExpNodePtr>(), $5.get<StmtNodePtr>());
       };

LastStatement 
    : RETURN Opt_ExpList {
        $$ = StmtNodePtr(new ReturnStmtNode($2.get<vector<ExpNodePtr> >()));
    }
    | BREAK {
        $$ = StmtNodePtr(new BreakStmtNode());
    }
    | CONTINUE {
        $$ = StmtNodePtr(new ContinueStmtNode());
    }
    ;
        
FuncName 
    : DotIDList 
    | DotIDList ':' ID {
        auto &info = $1.get<vector<string> >();
        info.push_back($3.get<string>());
        isDefiningMethod() = true;
        $$ = move($1);
    }
    ;

DotIDList 
    : ID {
        auto vec = vector<string>();
        vec.push_back($1.get<string>());
        $$ = vec;
    }
    | DotIDList '.' ID {
        $1.get<vector<string> >().push_back($3.get<string>());
        $$ = move($1);
    }
    ;

IDList 
    : ID {
        auto ids = vector<string>();
        ids.push_back($1.get<string>());
        $$ = ids;
    }
    | IDList ',' ID {
        $1.get<vector<string> >().push_back($3.get<string>());
        $$ = move($1);
    }
    ;

Opt_ExpList 
    :  {
        $$ = vector<ExpNodePtr>();
    }
    | ExpList
    ;
ExpList 
    : Exp {
        auto v = vector<ExpNodePtr>();
        v.push_back($1.get<ExpNodePtr>());
        $$ = v;
    }
    | ExpList ',' Exp {
        $1.get<vector<ExpNodePtr> >().push_back($3.get<ExpNodePtr>());
        $$ = move($1);
    }
    ;
Exp 
    : NIL {
        $$ = getConstExp(LuaValue::NIL);
    }
    | FALSE {
        $$ = getConstExp(LuaValue::FALSE);
    }
    | TRUE {
        $$ = getConstExp(LuaValue::TRUE);
    }
    | Number
    | Literal
    | OP_ARGS {
        $$ = ExpNodePtr(new ArgsTupleExpNode());
    }
    | Lambda
    | PrefixExp
    | TableConstructor
    | BinOp_Exp 
    | UnOp_Exp
    | '(' Exp ')' {
        $$ = $2;
    }
    ;

VarList 
    : Var {
        auto vec = vector<ExpNodePtr>();
        vec.push_back($1.get<ExpNodePtr>());
        $$ = vec;
    }
    | VarList ',' Var {
        $1.get<vector<ExpNodePtr> >().push_back($3.get<ExpNodePtr>());
        $$ = move($1);
    }
    ;
Var 
    : ID {
        $$ = getExpByIDName($1.get<string>());
        srcFileLine() = getTokenLine();
    }
    | PrefixExp '[' Exp ']' {
        $$ = ExpNodePtr(new FieldAccessExpNode($1.get<ExpNodePtr>(), $3.get<ExpNodePtr>()));
    }
    | PrefixExp '.' ID {
        $$ = ExpNodePtr(new FieldAccessExpNode($1.get<ExpNodePtr>(), getConstExp(LuaValue($3.get<string>().c_str()))));
    }
    ;

PrefixExp 
    : Var
    | FunctionCall  
    ;

FunctionCall 
    : PrefixExp Params {
        auto exp = new CallExpNode($1.get<ExpNodePtr>(), $2.get<vector<ExpNodePtr> >());
        exp->srcFile = srcFileName();
        exp->srcLine = srcFileLine();
        $$ = ExpNodePtr(exp);
    }
    | PrefixExp ':' ID Params {
        auto selfExp = $1.get<ExpNodePtr>();
        auto idExp = getConstExp(LuaValue($3.get<string>().c_str()));
        auto &params = $4.get<vector<ExpNodePtr> >();
        params.insert(params.begin(), selfExp);
        auto exp = new CallExpNode(ExpNodePtr(new FieldAccessExpNode(selfExp, idExp)), params);
        exp->srcFile = srcFileName();
        exp->srcLine = srcFileLine();
        $$ = ExpNodePtr(exp);
    }
    ;

Params 
    : '(' Opt_ExpList ')' {
        $$ = move($2);
    }
    | Literal {
        auto vec = vector<ExpNodePtr>();
        vec.push_back($1.get<ExpNodePtr>());
        $$ = vec;
    }
    | TableConstructor {
        auto vec = vector<ExpNodePtr>();
        vec.push_back($1.get<ExpNodePtr>());
        $$ = vec;
    }
    ;

Lambda : FUNCTION FuncBody {
        $$ = $2;
       }
       ;
FuncBody : '(' Opt_ArgList ')' {
            auto meta = LuaFunctionMetaPtr(new LuaFunctionMeta((int)LuaFunctionMeta::stack()->size()));
            LuaFunctionMeta::stack()->push(meta);
            SymbolTable::push();

            auto &args = $2.get<vector<string> >();
            meta->argCount = (int)args.size();
            if (isDefiningMethod()) {
                isDefiningMethod() = false;
                SymbolTable::top()->declareLocal("self");
                ++meta->argCount;
            }
            for (auto &arg : args) {
                SymbolTable::top()->declareLocal(arg);
            }
         } Block END  {
            auto meta = LuaFunctionMeta::stack()->top();
            meta->localCount = SymbolTable::top()->getLocalCount();
            LuaFunctionMeta::stack()->pop();
            SymbolTable::pop();
            meta->body = $5.get<StmtNodePtr>();
            $$ = ExpNodePtr(new LambdaExpNode(meta));
         }
         ;

Opt_ArgList 
    : {
        $$ = vector<string>();
    }
    | ArgList
    ;
ArgList 
    : IDList 
    | OP_ARGS {
        $$ = vector<string>();
    }
    | IDList ',' OP_ARGS {
        $$ = move($1);
    }
    ;

TableConstructor 
    : '{' Opt_FieldList '}' {
        auto &fields = $2.get<pair<vector<ExpNodePtr>, vector<pair<ExpNodePtr, ExpNodePtr> > > >();
        $$ = ExpNodePtr(new TableConstructorExpNode(fields.first, fields.second));
    }
    ;

Opt_FieldList 
    : {
        $$ = make_pair(vector<ExpNodePtr>(), vector<pair<ExpNodePtr, ExpNodePtr> >());
    }
    | FieldList
    | FieldList FieldSep
    ;
FieldList 
    : Field {
        auto fields = make_pair(vector<ExpNodePtr>(), vector<pair<ExpNodePtr, ExpNodePtr> >());
        if ($1.isTypeOf<ExpNodePtr>()) fields.first.push_back($1.get<ExpNodePtr>());
        else fields.second.push_back($1.get<pair<ExpNodePtr, ExpNodePtr> >());
        $$ = fields;
    }
    | FieldList FieldSep Field {
        auto &fields = $1.get<pair<vector<ExpNodePtr>, vector<pair<ExpNodePtr, ExpNodePtr> > > >();
        if ($3.isTypeOf<ExpNodePtr>()) fields.first.push_back($3.get<ExpNodePtr>());
        else fields.second.push_back($3.get<pair<ExpNodePtr, ExpNodePtr> >());
        $$ = move($1);
    }
    ;

FieldSep : ',' | ';' ;

Field 
    : Exp 
    | ID OP_ASSIGN Exp  {
        $$ = make_pair(getConstExp(LuaValue($1.get<string>().c_str())), $3.get<ExpNodePtr>());
    }
    | '[' Exp ']' OP_ASSIGN Exp {
        $$ = make_pair($2.get<ExpNodePtr>(), $5.get<ExpNodePtr>());
    }
    ;

BinOp_Exp 
    : Exp AND Exp {
        $$ = ExpNodePtr(new BinOpExpNode(BOP_And, $1.get<ExpNodePtr>(), $3.get<ExpNodePtr>()));
    }
    | Exp OR Exp {
        $$ = ExpNodePtr(new BinOpExpNode(BOP_Or, $1.get<ExpNodePtr>(), $3.get<ExpNodePtr>()));
    }
    | Exp OP_LESS Exp {
        $$ = ExpNodePtr(new BinOpExpNode(BOP_Less, $1.get<ExpNodePtr>(), $3.get<ExpNodePtr>()));
    }
    | Exp OP_LEQUAL Exp {
        $$ = ExpNodePtr(new BinOpExpNode(BOP_LessEq, $1.get<ExpNodePtr>(), $3.get<ExpNodePtr>()));
    }
    | Exp OP_GREATER Exp {
        $$ = ExpNodePtr(new BinOpExpNode(BOP_Greater, $1.get<ExpNodePtr>(), $3.get<ExpNodePtr>()));
    }
    | Exp OP_GEQUAL Exp {
        $$ = ExpNodePtr(new BinOpExpNode(BOP_GreaterEq, $1.get<ExpNodePtr>(), $3.get<ExpNodePtr>()));
    }
    | Exp OP_EQUAL Exp {
        $$ = ExpNodePtr(new BinOpExpNode(BOP_Equal, $1.get<ExpNodePtr>(), $3.get<ExpNodePtr>()));
    }
    | Exp OP_NEQUAL Exp {
        $$ = ExpNodePtr(new BinOpExpNode(BOP_NEqual, $1.get<ExpNodePtr>(), $3.get<ExpNodePtr>()));
    }
    | Exp OP_ADD Exp  {
        $$ = ExpNodePtr(new BinOpExpNode(BOP_Add, $1.get<ExpNodePtr>(), $3.get<ExpNodePtr>()));
    }
    | Exp OP_SUB Exp  {
        $$ = ExpNodePtr(new BinOpExpNode(BOP_Sub, $1.get<ExpNodePtr>(), $3.get<ExpNodePtr>()));
    }
    | Exp OP_MUL Exp {
        $$ = ExpNodePtr(new BinOpExpNode(BOP_Mul, $1.get<ExpNodePtr>(), $3.get<ExpNodePtr>()));
    }
    | Exp OP_DIV Exp {
        $$ = ExpNodePtr(new BinOpExpNode(BOP_Div, $1.get<ExpNodePtr>(), $3.get<ExpNodePtr>()));
    }
    | Exp OP_MOD Exp {
        $$ = ExpNodePtr(new BinOpExpNode(BOP_Mod, $1.get<ExpNodePtr>(), $3.get<ExpNodePtr>()));
    }
    | Exp OP_POWER Exp {
        $$ = ExpNodePtr(new BinOpExpNode(BOP_Pow, $1.get<ExpNodePtr>(), $3.get<ExpNodePtr>()));
    }
    | Exp OP_CONCAT Exp {
        $$ = ExpNodePtr(new BinOpExpNode(BOP_Concat, $1.get<ExpNodePtr>(), $3.get<ExpNodePtr>()));
    }
    ;
UnOp_Exp 
    : OP_SUB Exp {
        $$ = ExpNodePtr(new UnOpExpNode(UOP_Unm, $2.get<ExpNodePtr>()));
    }
    | NOT Exp {
        $$ = ExpNodePtr(new UnOpExpNode(UOP_Not, $2.get<ExpNodePtr>()));
    }
    | OP_LEN Exp {
        $$ = ExpNodePtr(new UnOpExpNode(UOP_Len, $2.get<ExpNodePtr>()));
    }
    ;

Number 
    : NUMBER1 {
        int num = 0;
        sscanf($1.get<string>().c_str(), "%x", &num);
        $$ = getConstExp(LuaValue(NumberType(num)));
    }
    | NUMBER2 {
        NumberType num = atof($1.get<string>().c_str());
        $$ = getConstExp(LuaValue(num));
    }
    | NUMBER3 {
        NumberType num = atof($1.get<string>().c_str());
        $$ = getConstExp(LuaValue(num));
    }
    ;
Literal
    : LITERAL1 {
        auto str = $1.get<string>();
        $$ = getConstExp(LuaValue(unEscape(str.substr(1, str.size() - 2)).c_str()));
    }
    | LITERAL2 {
        auto str = $1.get<string>();
        $$ = getConstExp(LuaValue(unEscape(str.substr(1, str.size() - 2)).c_str()));
    }
    | LITERAL3 {
        auto str = $1.get<string>();
        $$ = getConstExp(LuaValue(unEscape(str.substr(2, str.size() - 4)).c_str()));
    }
    ;

%%

static FunctionPtr _loadFile(FILE *f) {
    LuaFunctionMetaPtr meta(new LuaFunctionMeta((int)LuaFunctionMeta::stack()->size()));
    LuaFunctionMeta::stack()->push(meta);
    SymbolTable::push();

    yyrestart(f);
    yyparse();

    meta->localCount = SymbolTable::top()->getLocalCount();
    SymbolTable::pop();
    LuaFunctionMeta::stack()->pop();
    return FunctionPtr(
        LuaFunction::create(meta), [](LuaFunction *p){ p->releaseRef(); });
}
FunctionPtr loadFile(FILE *f) {
    srcFileName() = "[????]";
    return _loadFile(f);
}
FunctionPtr loadFile(const char *fname) {
    FILE *f = fopen(fname, "r");
    ASSERT1(f != NULL, format("Can not open file :%s", fname));
    try {
        srcFileName() = fname;
        auto r = _loadFile(f);
        fclose(f);
        return r;
    } catch(const exception&) {
        fclose(f);
        throw;
    }
}

static ExpNodePtr getLocalExp(const string& name) {
    auto index = SymbolTable::top()->getLocalIndex(name);
    auto nameIdx = LuaFunctionMeta::stack()->top()->getNameIndex(name);
    assert(index != -1);
    return ExpNodePtr(new LocalVarExpNode(index, nameIdx));
}
static ExpNodePtr getExpByIDName(const string& name) {
    auto localIdx = SymbolTable::top()->getLocalIndex(name);
    if (localIdx != -1) {
        return ExpNodePtr(new LocalVarExpNode(localIdx, LuaFunctionMeta::stack()->top()->getNameIndex(name)));
    } else {
        auto uvIdx = SymbolTable::top()->getUpValueIndex(name);
        if (uvIdx != -1) {
            return ExpNodePtr(new UpValueVarExpNode(uvIdx, LuaFunctionMeta::stack()->top()->getNameIndex(name)));
        } else {
            return ExpNodePtr(new GlobalVarExpNode(name));
        }
    }
}
static ExpNodePtr getConstExp(const LuaValue& v) {
    return ExpNodePtr(new ConstExpNode(FUNC_META, v));
}
static string unEscape(const string& s) {
    string r;
    for (int i = 0; i < (int)s.size(); ++i) {
        if (s[i] == '\\') {
            switch (s[++i]) {
            case 'r': r.push_back('\r'); break;
            case 'n': r.push_back('\n'); break;
            case 't': r.push_back('\t'); break;
            default: ASSERT(0) ; break;
            }
        }
        else r.push_back(s[i]);
    }
    return r;
}
static bool& isDefiningMethod() {
    static bool s_ins(false);
    return s_ins;
}
static string& srcFileName() {
    static string s_name;
    return s_name;
}
static int& srcFileLine() {
    static int s_line;
    return s_line;
}
