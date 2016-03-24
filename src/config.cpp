#include "config.hpp"
#include "core.hpp"

/* Stuff recarding configuration file
 * Note that this file is a mess
 * TODO: clean up and fix naming */

#define copyInto(x,y) std::memcpy(&(y), &(x), sizeof((y)))

#ifdef log
#undef log
#endif

#define log std::cout<<"[CC] "

namespace {
    /* removes whitespace from beginning and from end */
    std::string trim(std::string line) {
        int i = 0, j = line.length() - 1;
        for(; line[i] == ' ' && i < line.length(); i++);
        for(; line[j] == ' ' && j > i; j--);
        if(i > j)
            return "";

        return line.substr(i, j - i + 1);
    }

    void setDefaultOptions(PluginPtr p) {
        for(auto o : p->options)
            copyInto(o.second->def, o.second->data);
    }

    /* used for parsing of different values */
    std::stringstream theMachine;

    template<class T> T readValue(std::string val) {
        theMachine.str("");
        theMachine.clear();
        theMachine.str(val);

        T tmp;
        theMachine >> tmp;
        return tmp;
    }

    uint32_t getModFromStr(std::string value) {
        if(value == "Control")
            return WLC_BIT_MOD_CTRL;
        if(value == "Alt")
            return WLC_BIT_MOD_ALT;
        if(value == "Win")
            return WLC_BIT_MOD_LOGO;
        if(value == "Shift")
            return WLC_BIT_MOD_SHIFT;

        log << "modval not recognized" << std::endl;
        return 0;
    }

    uint getModsFromString(std::string value) {
        std::string cmod;
        uint mods = 0;
        for(auto c : value) {
            if(c == '<')
                cmod = "";
            else if(c == '>')
                mods |= getModFromStr(cmod);
            else
                cmod += c;
        }
        return mods;
    }

    template<> Color  readValue<Color>(std::string value) {
        Color c;
        std::stringstream str;
        str << value;
        str >> c.r >> c.g >> c.b;
        /* values are saved as ints, not floats */
        c.r /= 255.f, c.g /= 255.f, c.b /= 255.f;
        return c;
    }

    namespace {
        uint32_t readXKBKeyFromString(std::string value) {
            if(value == "a")
                return XKB_KEY_a;
            if(value == "A")
                return XKB_KEY_A;
            if(value == "b")
                return XKB_KEY_b;
            if(value == "B")
                return XKB_KEY_B;
            if(value == "c")
                return XKB_KEY_c;
            if(value == "C")
                return XKB_KEY_C;
            if(value == "d")
                return XKB_KEY_d;
            if(value == "D")
                return XKB_KEY_D;
            if(value == "e")
                return XKB_KEY_e;
            if(value == "E")
                return XKB_KEY_E;
            if(value == "f")
                return XKB_KEY_f;
            if(value == "F")
                return XKB_KEY_F;
            if(value == "g")
                return XKB_KEY_g;
            if(value == "G")
                return XKB_KEY_G;
            if(value == "h")
                return XKB_KEY_h;
            if(value == "H")
                return XKB_KEY_H;
            if(value == "i")
                return XKB_KEY_i;
            if(value == "I")
                return XKB_KEY_I;
            if(value == "j")
                return XKB_KEY_j;
            if(value == "J")
                return XKB_KEY_J;
            if(value == "k")
                return XKB_KEY_k;
            if(value == "K")
                return XKB_KEY_K;
            if(value == "l")
                return XKB_KEY_l;
            if(value == "L")
                return XKB_KEY_L;
            if(value == "m")
                return XKB_KEY_m;
            if(value == "M")
                return XKB_KEY_M;
            if(value == "n")
                return XKB_KEY_n;
            if(value == "N")
                return XKB_KEY_N;
            if(value == "o")
                return XKB_KEY_o;
            if(value == "O")
                return XKB_KEY_O;
            if(value == "p")
                return XKB_KEY_p;
            if(value == "P")
                return XKB_KEY_P;
            if(value == "q")
                return XKB_KEY_q;
            if(value == "Q")
                return XKB_KEY_Q;
            if(value == "r")
                return XKB_KEY_r;
            if(value == "R")
                return XKB_KEY_R;
            if(value == "s")
                return XKB_KEY_s;
            if(value == "S")
                return XKB_KEY_S;
            if(value == "t")
                return XKB_KEY_t;
            if(value == "T")
                return XKB_KEY_T;
            if(value == "u")
                return XKB_KEY_u;
            if(value == "U")
                return XKB_KEY_U;
            if(value == "v")
                return XKB_KEY_v;
            if(value == "V")
                return XKB_KEY_V;
            if(value == "w")
                return XKB_KEY_w;
            if(value == "W")
                return XKB_KEY_W;
            if(value == "x")
                return XKB_KEY_x;
            if(value == "X")
                return XKB_KEY_X;
            if(value == "y")
                return XKB_KEY_y;
            if(value == "Y")
                return XKB_KEY_Y;
            if(value == "z")
                return XKB_KEY_z;
            if(value == "Z")
                return XKB_KEY_Z;
            if(value == "KP_0")
                return XKB_KEY_KP_0;
            if(value == "KP_1")
                return XKB_KEY_KP_1;
            if(value == "KP_2")
                return XKB_KEY_KP_2;
            if(value == "KP_3")
                return XKB_KEY_KP_3;
            if(value == "KP_4")
                return XKB_KEY_KP_4;
            if(value == "KP_5")
                return XKB_KEY_KP_5;
            if(value == "KP_6")
                return XKB_KEY_KP_6;
            if(value == "KP_7")
                return XKB_KEY_KP_7;
            if(value == "KP_8")
                return XKB_KEY_KP_8;
            if(value == "KP_9")
                return XKB_KEY_KP_9;
            if(value == "0")
                return XKB_KEY_0;
            if(value == "1")
                return XKB_KEY_1;
            if(value == "2")
                return XKB_KEY_2;
            if(value == "3")
                return XKB_KEY_3;
            if(value == "4")
                return XKB_KEY_4;
            if(value == "5")
                return XKB_KEY_5;
            if(value == "6")
                return XKB_KEY_6;
            if(value == "7")
                return XKB_KEY_7;
            if(value == "8")
                return XKB_KEY_8;
            if(value == "9")
                return XKB_KEY_9;
            if(value == "F1")
                return XKB_KEY_F1;
            if(value == "F2")
                return XKB_KEY_F2;
            if(value == "F3")
                return XKB_KEY_F3;
            if(value == "F4")
                return XKB_KEY_F4;
            if(value == "F5")
                return XKB_KEY_F5;
            if(value == "F6")
                return XKB_KEY_F6;
            if(value == "F7")
                return XKB_KEY_F7;
            if(value == "F8")
                return XKB_KEY_F8;
            if(value == "F9")
                return XKB_KEY_F9;
            if(value == "F10")
                return XKB_KEY_F10;
            if(value == "F11")
                return XKB_KEY_F11;

            if(value == "F12")
                return XKB_KEY_F12;

            if(value == "Tab")
                return XKB_KEY_Tab;

            return -1;
        }

        std::string keyToString(uint32_t value) {
            if(value == XKB_KEY_a)
                return "a";
            if(value == XKB_KEY_A)
                return "A";
            if(value == XKB_KEY_b)
                return "b";
            if(value == XKB_KEY_B)
                return "B";
            if(value == XKB_KEY_c)
                return "c";
            if(value == XKB_KEY_C)
                return "C";
            if(value == XKB_KEY_d)
                return "d";
            if(value == XKB_KEY_D)
                return "D";
            if(value == XKB_KEY_e)
                return "e";
            if(value == XKB_KEY_E)
                return "E";
            if(value == XKB_KEY_f)
                return "f";
            if(value == XKB_KEY_F)
                return "F";
            if(value == XKB_KEY_g)
                return "g";
            if(value == XKB_KEY_G)
                return "G";
            if(value == XKB_KEY_h)
                return "h";
            if(value == XKB_KEY_H)
                return "H";
            if(value == XKB_KEY_i)
                return "i";
            if(value == XKB_KEY_I)
                return "I";
            if(value == XKB_KEY_j)
                return "j";
            if(value == XKB_KEY_J)
                return "J";
            if(value == XKB_KEY_k)
                return "k";
            if(value == XKB_KEY_K)
                return "K";
            if(value == XKB_KEY_l)
                return "l";
            if(value == XKB_KEY_L)
                return "L";
            if(value == XKB_KEY_m)
                return "m";
            if(value == XKB_KEY_M)
                return "M";
            if(value == XKB_KEY_n)
                return "n";
            if(value == XKB_KEY_N)
                return "N";
            if(value == XKB_KEY_o)
                return "o";
            if(value == XKB_KEY_O)
                return "O";
            if(value == XKB_KEY_p)
                return "p";
            if(value == XKB_KEY_P)
                return "P";
            if(value == XKB_KEY_q)
                return "q";
            if(value == XKB_KEY_Q)
                return "Q";
            if(value == XKB_KEY_r)
                return "r";
            if(value == XKB_KEY_R)
                return "R";
            if(value == XKB_KEY_s)
                return "s";
            if(value == XKB_KEY_S)
                return "S";
            if(value == XKB_KEY_t)
                return "t";
            if(value == XKB_KEY_T)
                return "T";
            if(value == XKB_KEY_u)
                return "u";
            if(value == XKB_KEY_U)
                return "U";
            if(value == XKB_KEY_v)
                return "v";
            if(value == XKB_KEY_V)
                return "V";
            if(value == XKB_KEY_w)
                return "w";
            if(value == XKB_KEY_W)
                return "W";
            if(value == XKB_KEY_x)
                return "x";
            if(value == XKB_KEY_X)
                return "X";
            if(value == XKB_KEY_y)
                return "y";
            if(value == XKB_KEY_Y)
                return "Y";
            if(value == XKB_KEY_z)
                return "z";
            if(value == XKB_KEY_Z)
                return "Z";
            if(value == XKB_KEY_KP_0)
                return "KP_0";
            if(value == XKB_KEY_KP_1)
                return "KP_1";
            if(value == XKB_KEY_KP_2)
                return "KP_2";
            if(value == XKB_KEY_KP_3)
                return "KP_3";
            if(value == XKB_KEY_KP_4)
                return "KP_4";
            if(value == XKB_KEY_KP_5)
                return "KP_5";
            if(value == XKB_KEY_KP_6)
                return "KP_6";
            if(value == XKB_KEY_KP_7)
                return "KP_7";
            if(value == XKB_KEY_KP_8)
                return "KP_8";
            if(value == XKB_KEY_KP_9)
                return "KP_9";
            if(value == XKB_KEY_0)
                return "0";
            if(value == XKB_KEY_1)
                return "1";
            if(value == XKB_KEY_2)
                return "2";
            if(value == XKB_KEY_3)
                return "3";
            if(value == XKB_KEY_4)
                return "4";
            if(value == XKB_KEY_5)
                return "5";
            if(value == XKB_KEY_6)
                return "6";
            if(value == XKB_KEY_7)
                return "7";
            if(value == XKB_KEY_8)
                return "8";
            if(value == XKB_KEY_9)
                return "9";
            if(value == XKB_KEY_F1)
                return "F1";
            if(value == XKB_KEY_F2)
                return "F2";
            if(value == XKB_KEY_F3)
                return "F3";
            if(value == XKB_KEY_F4)
                return "F4";
            if(value == XKB_KEY_F5)
                return "F5";
            if(value == XKB_KEY_F6)
                return "F6";
            if(value == XKB_KEY_F7)
                return "F7";
            if(value == XKB_KEY_F8)
                return "F8";
            if(value == XKB_KEY_F9)
                return "F9";
            if(value == XKB_KEY_F10)
                return "F10";
            if(value == XKB_KEY_F11)
                return "F11";

            return "";
        }
    }

    template<>Key readValue<Key>(std::string value) {
        Key key;
        key.mod = getModsFromString(value);
        int i = 0;
        for(i = value.length() - 1; i >= 0; i--) {
            if(value[i] == '>') break;
        }
        ++i;
        std::string keystr = value.substr(i, value.size() - i);
        key.key = readXKBKeyFromString(keystr);
        return key;
    }

    int Buttons [] = {0, BTN_LEFT, BTN_MIDDLE, BTN_RIGHT, BTN_GEAR_DOWN, BTN_GEAR_UP};

    template<>Button readValue<Button>(std::string value) {
        Button but;
        but.mod = getModsFromString(value);
        int i = 0;
        for(i = value.length() - 1; i >= 0; i--) {
            if(value[i] == '>') break;
        }
        i++;
        std::string tmp = value.substr(i, value.size() - i);
        tmp = trim(tmp); tmp = tmp.substr(1, tmp.size() - 1);

        if(!std::isdigit(tmp[0])){
            std::cout << "Error reading button binding!" << std::endl;
            but.button = 0;
            return but;
        }

        but.button = Buttons[tmp[0] - '0'];
        return but;
    }

    /* get internal type from string code */
    InternalOptionType getIOTFromString(std::string str) {
        if(str == "pln")
            return IOTPlain;
        if(str == "key")
            return IOTKey;
        if(str == "but")
            return IOTButton;
        if(str == "clr")
            return IOTColor;

        return IOTPlain;
    }

    /* return internal type and set name to the name
     * str should be trimmed! */
    InternalOptionType getIOTNameFromString(std::string str,
            std::string &name) {
        int i = 0;
        for(; i < str.length() && str[i] != '_'; i++);

        if(i == str.length()){
            log << "Warning! Option without a type!\n";
            name = str;
            return IOTPlain;
        }

        name = str.substr(i + 1, str.size() - i - 1);
        return getIOTFromString(str.substr(0, i));
    }

    /* checks if we can read the given option
     * in the requested format */
    bool isValidToRead(InternalOptionType itype, DataType type) {
        if(type == DataTypeKey && itype != IOTKey)
            return false;
        else if(type == DataTypeKey)
            return true;

        if(type == DataTypeButton && itype != IOTButton)
            return false;
        else if(type == DataTypeButton)
            return true;

        if(type == DataTypeColor && itype != IOTColor)
            return false;
        else if(type == DataTypeColor)
            return true;

        if(itype != IOTPlain)
            std::cout << "[WW] Type mismatch in config file.\n";

        /* other cases are too much to be checked */
        // TODO: implement some more invalid cases

        return true;
    }
}

Config::Config(std::string path) {
    this->path = path;
    printf("path to config %s\n", path.c_str());
    stream.open(path, std::ios::in | std::ios::out);
    if(!stream.is_open()) {
        printf("[EE] Failed to open config file\n");
        blocked = true;
    }
    else
        readConfig();
}

#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>


std::string get_home_dir() {
    const char *homedir;

    if ((homedir = getenv("HOME")) == NULL) {
        homedir = getpwuid(getuid())->pw_dir;
    }

    return homedir;
}

Config::Config() : Config(get_home_dir() + "/.config/firerc") { }

void Config::readConfig() {
    if(blocked)
        return;

    std::string line;
    std::string currentPluginName = "";
    std::string option, value;

    while(std::getline(stream, line)) {
        line = trim(line);
        if(!line.length())
            continue;

        if(line[0] == '[') {
            currentPluginName =
                line.substr(1, line.length() - 2);
            continue;
        }

        auto pos = line.find("=");
        if(pos == std::string::npos) {
            std::cout << "Warning - Garbage in config" << std::endl;
            continue;
        }

        option = trim(line.substr(0, pos));
        std::string realOption;
        auto type = getIOTNameFromString(option, realOption);
        value  = trim(line.substr(pos + 1, line.length() - pos));
        tree[currentPluginName][realOption] = Option{type, value};
    }
}

void Config::reset() {
    stream.close();
    tree.clear();
    stream.open(path, std::ios::in | std::ios::out);
}

void Config::setOptionsForPlugin(PluginPtr p) {
    auto name = p->owner->name;
    std::cout << "set options for " << name << std::endl;
    auto it = tree.find(name);
    if(it == tree.end()) {
        std::cout << "[WW] Plugin " << p->owner->name << " has no entry in config file." << std::endl;
        setDefaultOptions(p);
        return;
    }

    for(auto option : p->options) {
        auto oname = option.first;
        auto it = tree[name].find(oname);

        if(it == tree[name].end()) {
            std::cout << "copy default data" << std::endl;
            copyInto(p->options[oname]->def,
                    p->options[oname]->data);
            continue;
        }

        auto opt = it->second;
        auto reqType = p->options[oname]->type;

        if(!isValidToRead(opt.type, reqType)) {
            std::cout << "[EE] Type mismatch: \n";
            std::cout << "\t Plugin name:" << name
                << " Option name: " << oname << std::endl;

            copyInto(p->options[oname]->def,
                    p->options[oname]->data);
            continue;
        }

        std::cout << "reading " << oname << std::endl;
        auto data = opt.value;

        switch(reqType) {
            case DataTypeInt:
                p->options[oname]->data.ival =
                    readValue<int>(data);
                break;
            case DataTypeFloat:
                p->options[oname]->data.fval =
                    readValue<float>(data);
                break;
            case DataTypeBool:
                p->options[oname]->data.bval =
                    readValue<bool>(data);
                break;
            case DataTypeString:
                p->options[oname]->data.sval =
                    new std::string(data);
                break;
            case DataTypeColor:
                p->options[oname]->data.color =
                    new Color(readValue<Color>(data));
                break;
            case DataTypeKey:
                p->options[oname]->data.key =
                    new Key(readValue<Key>(data));
                break;
            case DataTypeButton:
                p->options[oname]->data.but =
                    new Button(readValue<Button>(data));
                break;
        }
    }
}
