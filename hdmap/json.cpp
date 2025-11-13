#include "json.h"

namespace LB {
	string JSON::s_s_;
	JSON::Array JSON::s_a_ = Array();
	JSON::Object JSON::s_o_ = Object();
	JSON::FunPtr JSON::fun_[256];

	int inti_fun() {
#if SZ_USE_X86_AVX512
		cout << "SZ_USE_X86_AVX512" << endl;;
#elif SZ_USE_X86_AVX2
		cout << "SZ_USE_X86_AVX2" << endl;;
#elif SZ_USE_ARM_NEON
		cout << "SZ_USE_ARM_NEON" << endl;;
#else
		cout << "SZ_NULL" << endl;;
#endif

		for (int c = 0; c < 256; ++c) {
			JSON::fun_[c] = nullptr;
		}

		JSON::fun_['/'] = &JSON::parse_note;
		JSON::fun_['"'] = &JSON::parse_quot;
		JSON::fun_['['] = &JSON::parse_a;
		JSON::fun_['{'] = &JSON::parse_o;
		JSON::fun_[']'] = &JSON::parse_end;
		JSON::fun_['}'] = &JSON::parse_end;
		JSON::fun_['+'] = &JSON::parse_num;
		JSON::fun_['-'] = &JSON::parse_num;
		JSON::fun_['.'] = &JSON::parse_num;

		for (int c = '0'; c <= '9'; ++c) {
			JSON::fun_[c] = &JSON::parse_num;
		}

		JSON::fun_['_'] = &JSON::parse_s;

		for (int c = 'a'; c <= 'z'; ++c) {
			JSON::fun_[c] = &JSON::parse_s;
		}

		for (int c = 'A'; c <= 'Z'; ++c) {
			JSON::fun_[c] = &JSON::parse_s;
		}

		return 0;
	}
	auto nouse = inti_fun();

	string doubleToString(double value, int precision) {
		long l = long(value);
		if (value == l) {
			return to_string(l);
		}

		ostringstream oss;
		oss << fixed << setprecision(precision) << value;
		string s = oss.str();

		// 去除末尾0
		while (s.back() == '0') {
			s.pop_back();
		}

		if (s.back() == '.') {
			s.pop_back();
		}

		return s;
	}

	JSON::JSON(const string& j) {
		const char* p = &j[0];
		parse(p, p + j.size());
	}

	JSON::JSON(const char* p, const char* pend) {
		parse(p, pend);
	}

	ostream& operator<<(ostream& os, const JSON& j) {
		os << j.dump();
		return os;
	}

	istream& operator>>(istream& is, JSON& j) {
		stringstream buffer;
		buffer << is.rdbuf();

		auto s = buffer.str();
		const char* p = &s[0];
		j.parse(p, p + s.size());

		return is;
	}

	void JSON::load(const string& file) {
		ifstream  fin(file);
		if (!fin.is_open()) {
			cout << "Open file error: " << file << endl;
			return;
		}

		fin >> *this;
		fin.close();
	}

	string JSON::get_str(const string& key_or_def) const {
		if (is_o_()) {
			string def;
			return get_str(key_or_def, def);
		}

		if (is_d_()) return doubleToString(d_());
		else if (is_b_()) return b_() ? "true" : "false";
		else if (is_s_()) return s_();
		return key_or_def;
	}

	string JSON::get_str(const size_t n, const string& def) const {
		if (is_a_()) {
			auto& a = a_();
			if (n < a.size()) {
				return a[n].get_str();
			}
		}

		return def;
	}

	string JSON::get_str(const string& key, const string& def) const {
		if (is_o_()) {
			auto& o = o_();
			if (o.contains(key)) {
				return o[key].get_str();
			}
		}

		return def;
	}

	// dump字符串
	void dump_str(const string& in, string& s) {
		s += '"';

		const char* p = &in[0];
		const char* pend = p + in.size();

		while (p != pend) {
			auto c = *p++;
			if (c == '"' || c == '\\') {
				s += '\\';
			}

			s += c;
		}

		s += '"';
	}

	JSON& JSON::operator[] (const size_t n) {
		if (is_a_()){
			auto& a = a_();
			if (a.size() > n) {
				return a[n];
			}
		}

		cout << "JSON is not array or index error!" << endl;
		throw std::runtime_error("JSON is not array or index error!");
	}

	void JSON::parse(const char*& p, const char* pend) {
		while (1) {
			if (p == pend) return;

			p = find_next(p, pend);
			if (!p) {
				p = pend;
				return;
			}

			int c = (unsigned char)*p++;
			auto fun = fun_[c];
			if (fun) {
				return (this->*fun)(p, pend);
			}
		}
	}

	// 支持/**/格式的注释
	void JSON::parse_note(const char*& p, const char* pend) {
		if (*p != '*') {
			return parse(p, pend);
		}
		
		++p;
		while (1) {
			p = find_char(p, pend, "*");
			if (!p || p + 1 >= pend) {
				p = pend;
				return;
			}

			if (*++p == '/') {
				return parse(++p, pend);
			}
		}
	}

	// 简单处理，仅仅去掉斜杠\转义，不考虑\r\n\t这样的情况
	void JSON::parse_quot(const char*& p, const char* pend) {
		v_ = string("");
		auto& s = s_();

		while (1) {
			auto pf = find_quot_end(p, pend);
			if (!pf) {
				s += string(p, pend);
				p = pend;
				return;
			}
			s += string(p, pf);
			p = pf + 1;
			if (p == pend) {
				return;
			}

			if (*pf == '"') {
				return;
			}
		}
	}

	void JSON::parse_s(const char*& p, const char* pend) {
		auto c = *(p - 1);

		if (c == 't') {
			if (strncmp(p, "rue", 3) == 0) {
				v_ = true;
				p += 3;
				return;
			}
		}
		else if (c == 'f') {
			if (strncmp(p, "alse", 4) == 0) {
				v_ = false;
				p += 4;
				return;
			}
		}

		v_ = string(1, c);
		auto& s = s_();

		while (1) {
			auto c = *p;

			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
				s.push_back(c);
				++p;
			}
			else {
				return;
			}
		}
	}

	double StringToDouble(const char* p, const char* pend) {
		// 使用高效的字符串到数字转换算法
		double result = 0.0;
		// 小数部分分开计算，否则误差会积累
		double fresult = 0.0;
		double fraction = 1.0;
		// 科学记数只支持整数
		long eresult = 0L;
		bool negative = false;
		bool in_fraction = false;

		// 先处理符号
		while (p != pend) {
			auto c = *p;
			if (c == '-') {
				negative = !negative;
				++p;
			}
			else if (c == '+') {
				++p;
			}

			break;
		}

		while (p != pend) {
			auto c = *p++;
			if (c == '.') {
				in_fraction = true;
			}
			else if (c == 'e' || c == 'E') {
				eresult = StringToDouble(p, pend);
				result += fresult;

				double mul;
				if (eresult < 0) {
					eresult = -eresult;
					mul = 0.1;
				} 
				else {
					mul = 10.0;
				}

				while (eresult--) {
					result *= mul;
				}
				
				return negative ? -result : result;
			}
			else if (in_fraction) {
				fraction *= 0.1;
				fresult += (c - '0') * fraction;
			}
			else {
				result = result * 10 + (c - '0');
			}
		}

		result += fresult;
		return negative ? -result : result;
	}

	void JSON::parse_num(const char*& p, const char* pend) {
		auto _p = p - 1;
		p = find_num_end(p, pend);

		if (!p) {
			p = pend;
		}

		v_ = StringToDouble(_p, p);
	}

	void JSON::parse_a(const char*& p, const char* pend) {
		while (1) {
			JSON& e = add_back();
			//JSON e;
			e.parse(p, pend);

			if (e.is_n_()) {
				pop_back();
				return;
			}
		}
	}

	void JSON::parse_o(const char*& p, const char* pend) {
		v_.emplace<Object>();
		auto& o = o_();

		while (1) {
			JSON key;
			key.parse(p, pend);
			if (key.is_n_()) {
				return;
			}

			JSON& v = o[key.get_str()];
			//JSON v;
			v.parse(p, pend);

			if (v.is_n_()) {
				v = "";
				return;
			}
		}
	}
}