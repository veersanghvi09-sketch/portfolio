// C++17 Console Portfolio Manager
// Single-file, no external libs. Features:
// - Assets (Stock/ETF/Mutual/Crypto/Other)
// - Transactions: BUY/SELL/DIVIDEND/DEPOSIT/WITHDRAW/FEES
// - FIFO lots, average cost, realized & unrealized PnL
// - Prices book (set/update current prices per asset)
// - Reports: holdings summary, P&L, transactions, cash ledger
// - Save/Load to JSON-like file; Export CSV
// - Undo last action
// - ANSI-colored pretty UI (can be disabled)
//
// Build:  g++ -std=gnu++17 -O2 portfolio.cpp -o portfolio
// Run:    ./portfolio

#include <bits/stdc++.h>
using namespace std;

namespace ui {
    bool use_color = true;
    string c(string code){ return ui::use_color ? ("\033[" + code + "m") : ""; }
    const string reset = "\033[0m";
    string bold(const string&s){ return c("1") + s + reset; }
    string dim(const string&s){ return c("2") + s + reset; }
    string green(const string&s){ return c("32") + s + reset; }
    string red(const string&s){ return c("31") + s + reset; }
    string yellow(const string&s){ return c("33") + s + reset; }
    string cyan(const string&s){ return c("36") + s + reset; }
    string magenta(const string&s){ return c("35") + s + reset; }
    string blue(const string&s){ return c("34") + s + reset; }
}

// ---------- Date helpers ----------
struct Date {
    int y=1970,m=1,d=1;
};

bool isLeap(int y){ return (y%400==0)|| (y%4==0 && y%100!=0); }
int mdays(int y,int m){ static int v[]={0,31,28,31,30,31,30,31,31,30,31,30,31}; return m==2? v[m]+(isLeap(y)?1:0) : v[m]; }

bool parseDate(const string& s, Date& out){
    // Expect YYYY-MM-DD
    if(s.size()!=10||s[4]!='-'||s[7]!='-') return false;
    try{
        out.y = stoi(s.substr(0,4));
        out.m = stoi(s.substr(5,2));
        out.d = stoi(s.substr(8,2));
    }catch(...){return false;}
    if(out.m<1||out.m>12) return false;
    if(out.d<1||out.d>mdays(out.y,out.m)) return false;
    return true;
}

long dateToSerial(const Date& dt){
    // days since 1970-01-01
    long days=0; for(int y=1970;y<dt.y;++y) days += isLeap(y)?366:365;
    for(int m=1;m<dt.m;++m) days += mdays(dt.y,m);
    days += dt.d-1; return days;
}

string dateToStr(const Date& d){
    auto pad2=[&](int x){ string s=to_string(x); if(s.size()<2) s="0"+s; return s; };
    return to_string(d.y)+"-"+pad2(d.m)+"-"+pad2(d.d);
}

// ---------- Domain types ----------

enum class AssetType { Stock, ETF, MutualFund, Crypto, Bond, Other };
string to_string(AssetType t){
    switch(t){
        case AssetType::Stock: return "Stock"; 
        case AssetType::ETF: return "ETF"; 
        case AssetType::MutualFund: return "MutualFund"; 
        case AssetType::Crypto: return "Crypto"; 
        case AssetType::Bond: return "Bond"; 
        default: return "Other"; 
    }
}
AssetType assetTypeFromStr(const string& s){
    string k=s; for(char&c: k) c=tolower(c);
    if(k=="stock") return AssetType::Stock;
    if(k=="etf") return AssetType::ETF;
    if(k=="mutualfund"||k=="mutual"||k=="mf") return AssetType::MutualFund;
    if(k=="crypto") return AssetType::Crypto;
    if(k=="bond") return AssetType::Bond;
    return AssetType::Other;
}

struct Asset {
    string ticker; // unique key
    string name;
    AssetType type=AssetType::Stock;
    string currency="INR";
};

enum class TxType { BUY, SELL, DIVIDEND, DEPOSIT, WITHDRAW, FEES };
string to_string(TxType t){
    switch(t){
        case TxType::BUY: return "BUY";
        case TxType::SELL: return "SELL";
        case TxType::DIVIDEND: return "DIVIDEND";
        case TxType::DEPOSIT: return "DEPOSIT";
        case TxType::WITHDRAW: return "WITHDRAW";
        case TxType::FEES: return "FEES";
    }
    return "?";
}

struct Transaction {
    string ticker; // which asset
    TxType type=TxType::BUY;
    Date date{};
    double qty=0.0; // units (for cash ledger, use ticker=="CASH")
    double price=0.0; // per unit price (ignored for DIVIDEND/DEPOSIT/WITHDRAW/FEES)
    double fees=0.0; // additional cost
    string note;
};

struct Lot { double qty=0.0; double cost=0.0; Date date{}; }; // cost = total cost incl fees

struct HoldingSummary {
    string ticker, name; AssetType type; string currency;
    double qty=0.0; double avg_cost=0.0; double mkt_price=0.0; 
    double mkt_value=0.0; double cost_basis=0.0; double unrealized=0.0; double pl_pct=0.0;
    double realized=0.0;
};

struct PortfolioState {
    // master data
    unordered_map<string,Asset> assets; // by ticker
    // current prices
    unordered_map<string,double> price;
    // transactions (in chronological order)
    vector<Transaction> txs;
    // realized P&L per ticker (accumulated from sells & dividends)
    unordered_map<string,double> realized;
};

// ---------- Serialization (simple JSON-like) ----------
string esc(const string& s){ string o; for(char c: s){ if(c=='"'||c=='\\') o.push_back('\\'), o.push_back(c); else if(c=='\n') o+="\\n"; else o.push_back(c);} return o; }

string serialize(const PortfolioState& st){
    stringstream ss; ss << "{\n";
    // assets
    ss << "  \"assets\": [\n";
    bool first=true; for(auto &kv: st.assets){ if(!first) ss<<",\n"; first=false; const auto&a=kv.second; 
        ss << "    {\"ticker\":\""<<esc(a.ticker) <<"\",\"name\":\""<<esc(a.name)
           <<"\",\"type\":\""<<esc(to_string(a.type)) <<"\",\"currency\":\""<<esc(a.currency)<<"\"}"; }
    ss << "\n  ],\n";
    // prices
    ss << "  \"prices\": {";
    first=true; for(auto &kv: st.price){ if(!first) ss<<","; first=false; ss << "\""<<esc(kv.first)<<"\":"<<kv.second; }
    ss << "},\n";
    // realized
    ss << "  \"realized\": {";
    first=true; for(auto &kv: st.realized){ if(!first) ss<<","; first=false; ss << "\""<<esc(kv.first)<<"\":"<<kv.second; }
    ss << "},\n";
    // txs
    ss << "  \"txs\": [\n";
    for(size_t i=0;i<st.txs.size();++i){ auto&t=st.txs[i];
        ss << "    {\"ticker\":\""<<esc(t.ticker) <<"\",\"type\":\""<<to_string(t.type)
           <<"\",\"date\":\""<<dateToStr(t.date) <<"\",\"qty\":"<<t.qty
           <<",\"price\":"<<t.price<<",\"fees\":"<<t.fees<<",\"note\":\""<<esc(t.note)<<"\"}";
        if(i+1<st.txs.size()) ss<<","; ss<<"\n";
    }
    ss << "  ]\n";
    ss << "}\n"; return ss.str();
}

// super-lightweight parser for our own format (not robust JSON) but ok for this app
bool loadFromString(const string& data, PortfolioState& st){
    st = PortfolioState();
    auto findAll = [&](const string& key){ vector<pair<size_t,size_t>> spans; size_t pos=0; while(true){ pos=data.find(key,pos); if(pos==string::npos) break; size_t start=data.find('{',pos); if(start==string::npos) break; int bal=1; size_t i=start+1; for(;i<data.size()&&bal;i++){ if(data[i]=='{') bal++; else if(data[i]=='}') bal--; } if(!bal){ spans.push_back({start,i}); pos=i; } else break; } return spans; };
    // parse assets array
    auto assetsArrPos = data.find("\"assets\""); if(assetsArrPos==string::npos) return false; 
    auto assetsBracket = data.find('[',assetsArrPos); auto assetsEnd = data.find(']',assetsBracket);
    string assetsChunk = data.substr(assetsBracket+1, assetsEnd-assetsBracket-1);
    // split objects by '},{'
    string tmp=assetsChunk; 
    vector<string> objs; {
        size_t i=0; int bal=0; size_t last=0; for(;i<tmp.size();++i){ if(tmp[i]=='{') { if(bal==0) last=i; bal++; }
            else if(tmp[i]=='}'){ bal--; if(bal==0){ objs.push_back(tmp.substr(last,i-last+1)); } }
        }
    }
    auto getField=[&](const string& obj, const string& key){ string pat="\""+key+"\"\s*:\s*\""; auto p=obj.find(pat); if(p==string::npos) return string(""); p+=pat.size(); string val; for(size_t i=p;i<obj.size();++i){ char c=obj[i]; if(c=='\\'){ if(i+1<obj.size()) { val.push_back(obj[i+1]); i++; } }
            else if(c=='"') break; else val.push_back(c);
        } return val; };
    for(auto &o: objs){ Asset a; a.ticker=getField(o,"ticker"); a.name=getField(o,"name"); a.currency=getField(o,"currency"); a.type=assetTypeFromStr(getField(o,"type")); if(!a.ticker.empty()) st.assets[a.ticker]=a; }
    // prices
    auto pricesPos=data.find("\"prices\""); if(pricesPos!=string::npos){ auto lb=data.find('{',pricesPos); auto rb=data.find('}',lb); string body=data.substr(lb+1,rb-lb-1); stringstream ss(body); string item; while(getline(ss,item,',')){ auto col=item.find(':'); if(col==string::npos) continue; string k=item.substr(0,col); string v=item.substr(col+1); auto trim=[&](string s){ s.erase(remove_if(s.begin(),s.end(),[](char c){return isspace((unsigned char)c);}), s.end()); if(!s.empty()&&s.front()=='"') s.erase(s.begin()); if(!s.empty()&&s.back()=='"') s.pop_back(); return s; }; k=trim(k); v=trim(v); if(!k.empty()&&!v.empty()) st.price[k]=stod(v); }
    }
    // realized
    auto realizedPos=data.find("\"realized\""); if(realizedPos!=string::npos){ auto lb=data.find('{',realizedPos); auto rb=data.find('}',lb); string body=data.substr(lb+1,rb-lb-1); stringstream ss(body); string item; while(getline(ss,item,',')){ auto col=item.find(':'); if(col==string::npos) continue; string k=item.substr(0,col); string v=item.substr(col+1); auto trim=[&](string s){ s.erase(remove_if(s.begin(),s.end(),[](char c){return isspace((unsigned char)c);}), s.end()); if(!s.empty()&&s.front()=='"') s.erase(s.begin()); if(!s.empty()&&s.back()=='"') s.pop_back(); return s; }; k=trim(k); v=trim(v); if(!k.empty()&&!v.empty()) st.realized[k]=stod(v); }
    }
    // txs
    auto txsPos=data.find("\"txs\""); if(txsPos!=string::npos){ auto lb=data.find('[',txsPos); auto rb=data.find(']',lb); string body=data.substr(lb+1,rb-lb-1);
        // split objects
        vector<string> to; size_t i=0; int bal=0; size_t last=0; for(;i<body.size();++i){ if(body[i]=='{'){ if(bal==0) last=i; bal++; } else if(body[i]=='}'){ bal--; if(bal==0) to.push_back(body.substr(last,i-last+1)); } }
        auto getS=[&](const string& obj,const string& key){ return getField(obj,key); };
        auto getD=[&](const string& obj,const string& key){ string pat="\""+key+"\"\s*:\s*"; auto p=obj.find(pat); if(p==string::npos) return 0.0; p+=pat.size(); string num; for(size_t i=p;i<obj.size();++i){ char c=obj[i]; if((c>='0'&&c<='9')||c=='-'||c=='.'||c=='e'||c=='E') num.push_back(c); else if(!num.empty()) break; } return stod(num); };
        for(auto &o: to){ Transaction t; t.ticker=getS(o,"ticker"); string ts=getS(o,"type"); string td=getS(o,"date"); parseDate(td,t.date); if(ts=="BUY") t.type=TxType::BUY; else if(ts=="SELL") t.type=TxType::SELL; else if(ts=="DIVIDEND") t.type=TxType::DIVIDEND; else if(ts=="DEPOSIT") t.type=TxType::DEPOSIT; else if(ts=="WITHDRAW") t.type=TxType::WITHDRAW; else t.type=TxType::FEES; t.qty=getD(o,"qty"); t.price=getD(o,"price"); t.fees=getD(o,"fees"); t.note=getS(o,"note"); if(!t.ticker.empty()) st.txs.push_back(t); }
    }
    return true;
}

// ---------- Portfolio engine ----------

struct Engine {
    PortfolioState st;
    vector<string> undo_stack; // serialized states for undo

    void push_undo(){ undo_stack.push_back(serialize(st)); if(undo_stack.size()>50) undo_stack.erase(undo_stack.begin()); }
    bool undo(){ if(undo_stack.empty()) return false; string last=undo_stack.back(); undo_stack.pop_back(); loadFromString(last, st); return true; }

    void ensureAsset(const string& ticker){ if(!st.assets.count(ticker)){ Asset a; a.ticker=ticker; a.name=ticker; a.type=AssetType::Stock; a.currency="INR"; st.assets[ticker]=a; } }

    void addAsset(){
        cout<<ui::bold("\nAdd Asset\n");
        string t; cout<<"Ticker (unique): "; getline(cin,t); if(t.empty()){ cout<<"Cancelled.\n"; return; }
        if(st.assets.count(t)){ cout<<ui::yellow("Already exists. Updated instead.\n"); }
        Asset a; a.ticker=t; cout<<"Name: "; getline(cin,a.name); if(a.name.empty()) a.name=t;
        cout<<"Type (Stock/ETF/MutualFund/Crypto/Bond/Other): "; string ty; getline(cin,ty); a.type=assetTypeFromStr(ty);
        cout<<"Currency (default INR): "; string cur; getline(cin,cur); if(!cur.empty()) a.currency=cur; st.assets[t]=a;
        cout<<ui::green("Saved asset.\n");
    }

    void setPrice(){
        cout<<ui::bold("\nSet/Update Price\n");
        string t; cout<<"Ticker: "; getline(cin,t); if(!st.assets.count(t)){ cout<<ui::red("Unknown ticker. Add asset first.\n"); return; }
        cout<<"Price per unit: "; string sp; getline(cin,sp); double p=stod(sp); st.price[t]=p; cout<<ui::green("Price updated.\n");
    }

    void addTx(){
        cout<<ui::bold("\nAdd Transaction\n");
        string t; cout<<"Ticker (or CASH for cash ledger): "; getline(cin,t); if(t.empty()) return; ensureAsset(t);
        cout<<"Type [BUY/SELL/DIVIDEND/DEPOSIT/WITHDRAW/FEES]: "; string ty; getline(cin,ty);
        TxType tp=TxType::BUY; string k=ty; for(char&c:k)c=toupper(c); if(k=="SELL")tp=TxType::SELL; else if(k=="DIVIDEND")tp=TxType::DIVIDEND; else if(k=="DEPOSIT")tp=TxType::DEPOSIT; else if(k=="WITHDRAW")tp=TxType::WITHDRAW; else if(k=="FEES")tp=TxType::FEES;
        cout<<"Date (YYYY-MM-DD): "; string ds; getline(cin,ds); Date d; if(!parseDate(ds,d)){ cout<<ui::red("Invalid date.\n"); return; }
        cout<<"Quantity (units or amount for CASH): "; string sq; getline(cin,sq); double q=stod(sq);
        double price=0.0; if(tp==TxType::BUY||tp==TxType::SELL){ cout<<"Price per unit: "; string sp; getline(cin,sp); price=stod(sp);} 
        cout<<"Fees (0 if none): "; string sf; getline(cin,sf); double f= sf.empty()?0.0:stod(sf); 
        cout<<"Note (optional): "; string note; getline(cin,note);
        Transaction tx{t,tp,d,q,price,f,note}; push_undo(); st.txs.push_back(tx); sort(st.txs.begin(), st.txs.end(),[](auto&a,auto&b){ return dateToSerial(a.date)<dateToSerial(b.date); });
        cout<<ui::green("Transaction added.\n");
    }

    void listTx(const string& ticker=""){ cout<<ui::bold("\nTransactions\n"); cout<<left<<setw(10)<<"Date"<<setw(12)<<"Ticker"<<setw(10)<<"Type"<<setw(10)<<"Qty"<<setw(12)<<"Price"<<setw(10)<<"Fees"<<"Note\n"; cout<<string(80,'-')<<"\n"; for(auto&t: st.txs){ if(!ticker.empty() && t.ticker!=ticker) continue; cout<<left<<setw(10)<<dateToStr(t.date)<<setw(12)<<t.ticker<<setw(10)<<to_string(t.type)<<setw(10)<<fixed<<setprecision(4)<<t.qty<<setw(12)<<setprecision(2)<<t.price<<setw(10)<<t.fees<<t.note<<"\n"; } }

    struct Computed { unordered_map<string, vector<Lot>> lots; unordered_map<string,double> realized; double cash=0.0; };

    Computed compute(){
        Computed c; c.lots.clear(); c.realized.clear(); c.cash=0.0;
        // initialize realized from state (persisted dividends etc.)
        c.realized = st.realized; 
        // process chronologically
        for(const auto& tx : st.txs){
            if(tx.ticker=="CASH"){
                if(tx.type==TxType::DEPOSIT) c.cash += tx.qty;
                else if(tx.type==TxType::WITHDRAW) c.cash -= tx.qty;
                else if(tx.type==TxType::FEES) c.cash -= tx.qty; // treat qty as amount
                continue;
            }
            auto &lots = c.lots[tx.ticker];
            if(tx.type==TxType::BUY){
                double totalCost = tx.qty*tx.price + tx.fees;
                lots.push_back(Lot{tx.qty,totalCost,tx.date});
                c.cash -= totalCost;
            } else if(tx.type==TxType::SELL){
                double qtyToSell = tx.qty; double proceeds = tx.qty*tx.price - tx.fees; c.cash += proceeds;
                double realizedPnL = 0.0;
                // FIFO consume lots
                while(qtyToSell>1e-9 && !lots.empty()){
                    auto &lot = lots.front(); double take = min(lot.qty, qtyToSell);
                    double lotAvg = (lot.qty>0? lot.cost/lot.qty : 0.0);
                    realizedPnL += take * (tx.price - lotAvg);
                    lot.qty -= take; lot.cost -= lotAvg * take; qtyToSell -= take;
                    if(lot.qty<=1e-9) lots.erase(lots.begin());
                }
                if(qtyToSell>1e-9){ // selling more than held
                    realizedPnL += qtyToSell * tx.price; // treat as short sale profit baseline 0
                    qtyToSell=0;
                }
                c.realized[tx.ticker] += realizedPnL - tx.fees; // subtract fees (already from proceeds)
            } else if(tx.type==TxType::DIVIDEND){
                c.cash += tx.qty; c.realized[tx.ticker] += tx.qty; // treat dividend as realized gain
            } else if(tx.type==TxType::FEES){
                c.cash -= tx.qty; // fee amount in qty
            }
        }
        return c;
    }

    vector<HoldingSummary> holdings(){
        auto c = compute();
        vector<HoldingSummary> out; out.reserve(c.lots.size());
        for(auto &kv : c.lots){
            const string& t = kv.first; const auto& lots = kv.second; const auto& a = st.assets[t];
            double qty=0, cost=0; for(auto &l: lots){ qty += l.qty; cost += l.cost; }
            double price = st.price.count(t)? st.price[t] : 0.0; double mkt=qty*price; 
            double avg = qty>0? cost/qty : 0.0; double unrl = mkt - cost; double pct = (cost>0? (unrl/cost*100.0) : 0.0);
            double realized = st.realized.count(t)? st.realized[t] : 0.0;
            out.push_back(HoldingSummary{t,a.name,a.type,a.currency,qty,avg,price,mkt,cost,unrl,pct,realized});
        }
        sort(out.begin(), out.end(), [](const auto&x,const auto&y){ return x.mkt_value>y.mkt_value; });
        return out;
    }

    void showSummary(){
        auto c = compute();
        auto hs = holdings();
        cout<<ui::bold("\nPortfolio Summary\n");
        cout<<left<<setw(10)<<"Ticker"<<setw(16)<<"Name"<<setw(10)<<"Type"<<setw(10)<<"Qty"<<setw(12)<<"AvgCost"<<setw(12)<<"Price"<<setw(12)<<"Value"<<setw(12)<<"Cost"<<setw(12)<<"Unreal"<<setw(8)<<"%"<<setw(12)<<"Realized"<<"\n";
        cout<<string(120,'-')<<"\n";
        double totVal=0, totCost=0, totUnrl=0, totRe=0;
        for(auto &h: hs){
            totVal+=h.mkt_value; totCost+=h.cost_basis; totUnrl+=h.unrealized; totRe+=h.realized;
            cout<<left<<setw(10)<<h.ticker<<setw(16)<<h.name.substr(0,15)<<setw(10)<<to_string(h.type).substr(0,9)
                <<setw(10)<<fixed<<setprecision(4)<<h.qty<<setw(12)<<setprecision(2)<<h.avg_cost<<setw(12)<<h.mkt_price
                <<setw(12)<<h.mkt_value<<setw(12)<<h.cost_basis;
            string u = (h.unrealized>=0? ui::green(to_string(h.unrealized)) : ui::red(to_string(h.unrealized)));
            cout<<setw(12)<<u<<setw(8)<<fixed<<setprecision(2)<<h.pl_pct<<setw(12)<<h.realized<<"\n";
        }
        cout<<string(120,'-')<<"\n";
        cout<<ui::bold("Totals  ")<<"Value="<<totVal<<"  Cost="<<totCost<<"  Unrl="<<(totUnrl>=0? ui::green(to_string(totUnrl)): ui::red(to_string(totUnrl)))
            <<"  Realized="<<totRe<<"  Cash="<<c.cash<<"\n";
    }

    void exportCSV(){
        cout<<"File name (e.g., holdings.csv): "; string fn; getline(cin,fn); if(fn.empty()) return; 
        ofstream f(fn); if(!f){ cout<<ui::red("Cannot write file.\n"); return; }
        auto c=compute(); auto hs=holdings();
        f<<"Ticker,Name,Type,Currency,Qty,AvgCost,Price,Value,Cost,Unreal,Pct,Realized\n";
        f.setf(std::ios::fixed); f<<setprecision(4);
        for(auto &h: hs){ f<<h.ticker<<","<<'"'<<h.name<<'"'<<","<<to_string(h.type)<<","<<h.currency<<","<<h.qty<<","<<setprecision(2)<<h.avg_cost
                           <<","<<h.mkt_price<<","<<h.mkt_value<<","<<h.cost_basis<<","<<h.unrealized<<","<<setprecision(2)<<h.pl_pct<<","<<h.realized<<"\n"; }
        f<<"\nCash,"<<c.cash<<"\n";
        cout<<ui::green("Exported CSV.\n");
    }

    void save(){ cout<<"Save to file (e.g., portfolio.json): "; string fn; getline(cin,fn); if(fn.empty()) return; ofstream f(fn); if(!f){ cout<<ui::red("Cannot open file.\n"); return; } f<<serialize(st); cout<<ui::green("Saved.\n"); }
    void load(){ cout<<"Load from file: "; string fn; getline(cin,fn); ifstream f(fn); if(!f){ cout<<ui::red("Cannot open file.\n"); return; } stringstream ss; ss<<f.rdbuf(); if(loadFromString(ss.str(), st)) cout<<ui::green("Loaded.\n"); else cout<<ui::red("Failed to parse.\n"); }

    void removeTx(){ listTx(); cout<<"\nEnter index to remove (1..N): "; string si; getline(cin,si); int idx=stoi(si); if(idx<1||idx>(int)st.txs.size()){ cout<<ui::red("Invalid index.\n"); return; } push_undo(); st.txs.erase(st.txs.begin()+idx-1); cout<<ui::green("Removed.\n"); }

    void listTxWithIndex(){ cout<<ui::bold("\nTransactions (Indexed)\n"); cout<<left<<setw(5)<<"#"<<setw(10)<<"Date"<<setw(12)<<"Ticker"<<setw(10)<<"Type"<<setw(10)<<"Qty"<<setw(12)<<"Price"<<setw(10)<<"Fees"<<"Note\n"; cout<<string(90,'-')<<"\n"; for(size_t i=0;i<st.txs.size();++i){ auto&t=st.txs[i]; cout<<left<<setw(5)<<(i+1)<<setw(10)<<dateToStr(t.date)<<setw(12)<<t.ticker<<setw(10)<<to_string(t.type)<<setw(10)<<fixed<<setprecision(4)<<t.qty<<setw(12)<<setprecision(2)<<t.price<<setw(10)<<t.fees<<t.note<<"\n"; } }

    void menu(){
        while(true){
            cout<<"\n"<<ui::bold("==== Portfolio Manager ====")<<"\n";
            cout<<"1) Add/Update Asset\n2) Set/Update Price\n3) Add Transaction\n4) Show Summary\n5) List Transactions\n6) Save\n7) Load\n8) Export CSV\n9) Remove Transaction\n10) Undo Last\n11) Toggle Color\n0) Exit\nChoice: ";
            string ch; getline(cin,ch); if(ch=="1") addAsset();
            else if(ch=="2") setPrice();
            else if(ch=="3") addTx();
            else if(ch=="4") showSummary();
            else if(ch=="5") { listTxWithIndex(); }
            else if(ch=="6") save();
            else if(ch=="7") load();
            else if(ch=="8") exportCSV();
            else if(ch=="9") removeTx();
            else if(ch=="10"){ if(undo()) cout<<ui::yellow("Undone.\n"); else cout<<ui::red("Nothing to undo.\n"); }
            else if(ch=="11"){ ui::use_color=!ui::use_color; cout<<"Color: "<<(ui::use_color?"ON":"OFF")<<"\n"; }
            else if(ch=="0"||cin.eof()) break;
            else cout<<ui::red("Invalid choice.\n");
        }
    }
};

int main(){
    ios::sync_with_stdio(false); cin.tie(nullptr);
    cout<<ui::cyan("\nWelcome to the C++ Portfolio Manager ✨\n");
    cout<<ui::dim("Tip: Add CASH transactions (DEPOSIT/WITHDRAW/FEES) to track cash. Set prices for tickers to see market values.\n");
    Engine e; e.menu();
    cout<<ui::cyan("\nBye!\n");
    return 0;
}
