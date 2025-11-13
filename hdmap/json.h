#pragma once
#include <string>
#include <string_view>
#include <string.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <map>
#include <list>
#include <forward_list>
#include <deque>
#include <variant>
#include <chrono>
#include <tuple>
#include <functional>
#include <type_traits>
#include <memory>
#include <iterator>
#include "stringzilla.h"

using namespace std;

// 检测是否有 fun 成员函数
// &std::declval<T>().fun 不关注参数，但若有重载会找不到，可采用下面方式（但函数有const等，windows和ubuntu差异大）
// std::declval<T>().fun(xxx) xxx为参数
#define HAS_MEMBER_FUNCTION(fun) \
template<typename T, typename = void> \
struct has_##fun : std::false_type {}; \
\
template<typename T> \
struct has_##fun<T, void_t<decltype(std::declval<T>().fun())>> : std::true_type {};

namespace LB {
	double StringToDouble(const char* p, const char* pend);
	string doubleToString(double value, int precision = 9);
	// 字符串两边加"，并且字符串中间"\转义处理
	void dump_str(const string&, string&);

	template<typename T>
	string to_string_h0(T t, int s = 2) {
		string r = std::to_string(t);
		if (r.size() >= s) {
			return r;
		}
		return std::string(s - r.size(), '0') + r;
	}

	template<typename T>
	string to_string_t0(T t, int s = 2) {
		ostringstream oss;
		oss << fixed << setprecision(s) << t;
		return oss.str();
	}


	// 使用智能指针存储
	// 1. vector使用智能指针可以避免拷贝
	// 2. deque在类型不明确时只能使用智能指针
	template<typename T, template<typename...> typename M=deque>
	class ArrayPtr {
	private:
		using ValueType = M<unique_ptr<T>>;
		using Iterator = typename ValueType::iterator;
		ValueType v_;

	public:
		ArrayPtr() = default;
		size_t size() const { return v_.size(); }

		T& operator[](const size_t n) {
			return *v_[n];
		}

		T& add_back() {
			v_.push_back(make_unique<T>());
			return *v_.back();
		}

		void pop_back() {
			v_.pop_back();
		}

		class iterator {
		private:
			Iterator it_;

		public:
			using iterator_category = forward_iterator_tag;
			using value_type = T;
			using difference_type = ptrdiff_t;
			using pointer = T*;
			using reference = T&;

			iterator(Iterator it) : it_(it) {}

			bool operator!=(const iterator& other) const {
				return it_ != other.it_;
			}

			bool operator==(const iterator& other) const {
				return it_ == other.it_;
			}

			reference operator*() {
				return **it_;
			}

			iterator& operator++() {
				++it_;
				return *this;
			}
		};

		iterator begin() { return v_.begin(); }
		iterator end() { return v_.end(); }
	};

	// 带未指针的forward_list
	template<typename T>
	class ForwardList {
	private:
		using iterator = typename forward_list<T>::iterator;
		forward_list<T> list_;
		size_t size_ = 0;
		iterator tail_; // 指向最后一个元素
		iterator tail2_; // 指向最后第二个元素

	public:
		ForwardList() = default;
		size_t size() const { return size_; }

		T& operator[](const size_t n) {
			static size_t i = -1;
			static iterator it;
			static decltype(this) th = nullptr;

			if (i > n || this != th) {
				i = 0;
				it = list_.begin();
				th = this;
			}

			while (i < n) {
				++i;
				++it;
			}

			return *it;
		}

		// 数据只能通过add_back在尾部插入
		T& add_back() {
			if (++size_ == 1) {
				list_.push_front(T());
				tail_ = list_.begin();
			}
			else {
				list_.insert_after(tail_, T());
				tail2_ = tail_++;
			}

			return *tail_;
		}

		// 数据只能通过pop_back在尾部删除，并且调用时要保证已经有数据
		void pop_back() {
			if (--size_ == 0) list_.clear();

			list_.erase_after(tail2_);
			tail_ = tail2_;
		}

		iterator begin() { return list_.begin(); }
		iterator end() { return list_.end(); }
	};

	// 固定大小vector，避免数据拷贝
	template<typename Value, size_t N>
	class Vector {
	private:
		using ValueType = deque<unique_ptr<vector<Value>>>;
		using Iterator = typename ValueType::iterator;
		ValueType v_;
		size_t size_ = 0;

	public:
		size_t size() const { return size_; }

		Value& operator[](const size_t n) {
			return (*v_[n / N])[n % N];
		}

		Value& add_back() {
			auto chunk_index = size_++ / N;

			auto add = [&]() -> auto& {
				v_.push_back(make_unique<vector<Value>>());

				auto& b = v_.back();
				b->reserve(N);
			
				return b;
				};

			auto& p = chunk_index == v_.size() ? add() : v_[chunk_index];
			p->resize(p->size() + 1);
			return p->back();
		}

		void pop_back() {
			// 已经分配的unique_ptr<vector<Value>>不pop
			--size_;
			v_[size_ / N]->pop_back();
		}

		class iterator {
		private:
			Iterator it_;
			size_t i_; // 可以等于N

		public:
			using iterator_category = forward_iterator_tag;
			using value_type = Value;
			using difference_type = ptrdiff_t;
			using pointer = Value*;
			using reference = Value&;

			iterator(Iterator it, size_t i) : it_(it), i_(i) {}

			bool operator!=(const iterator& other) const {
				return it_ != other.it_ || i_ != other.i_;
			}

			bool operator==(const iterator& other) const {
				return it_ == other.it_ && i_ == other.i_;
			}

			reference operator*() {
				if (i_ == N) {
					++it_;
					i_ = 0;
				}

				return (**it_)[i_];
			}

			iterator& operator++() {
				if (i_ == N) {
					++it_;
					i_ = 1;
				}
				else {
					// i_可以等于N，避免最后一个++出错
					++i_;
				}
				return *this;

			}
		};

		iterator begin() {
			return iterator(v_.begin(), 0);
		}

		iterator end() {
			if (size_ == 0) return iterator(v_.begin(), 0);

			auto it = v_.end();
			while (true) {
				--it;
				auto size = (*it)->size();
				if (size) return iterator(it, size);
			}
		}
	};

	// 按插入顺序排列的map
	template<typename Key, typename Value>
	class InsertOrderMap {
	private:
		using ListType = list<pair<Key, Value>>;
		using MapType = unordered_map<Key, typename ListType::iterator>;

		ListType order_list;
		MapType lookup_map;

	public:
		void insert_not(const Key& key, const Value& value) {
			auto it = lookup_map.find(key);
			if (it != lookup_map.end()) {
				// 键已存在，更新值
				it->second->second = value;
			}
			else {
				// 插入新元素
				order_list.emplace_back(key, value);
				lookup_map[key] = prev(order_list.end());
			}
		}

		Value& operator[](const Key& key) {
			auto it = lookup_map.find(key);
			if (it != lookup_map.end()) {
				return it->second->second;
			}
			else {
				order_list.emplace_back(key, Value());
				auto list_it = prev(order_list.end());
				lookup_map[key] = list_it;
				return list_it->second;
			}
		}

		Value& at(const Key& key) const {
			auto it = lookup_map.find(key);
			if (it != lookup_map.end()) {
				return it->second->second;
			}

			cout << "InsertOrderMap key error!" << key << endl;
			throw std::runtime_error("InsertOrderMap key error!");
		}

		bool contains(const Key& key) const {
			return lookup_map.find(key) != lookup_map.end();
		}

		size_t erase(const Key& key) {
			auto it = lookup_map.find(key);
			if (it != lookup_map.end()) {
				order_list.erase(it->second);
				lookup_map.erase(it);
				return 1;
			}
			return 0;
		}

		// 迭代器 - 按插入顺序遍历
		auto begin() { return order_list.begin(); }
		auto end() { return order_list.end(); }
		auto begin() const { return order_list.begin(); }
		auto end() const { return order_list.end(); }

		size_t size() const { return order_list.size(); }
		bool empty() const { return order_list.empty(); }
	};

	class JSON {
	public:
		using Number = double;
		using Array = ArrayPtr<JSON>;           // 应该最快，不过总体差别不大
		//using Array = ArrayPtr<JSON, vector>; // 其次
		//using Array = ForwardList<JSON>;      // 最慢
		//using Array = Vector<JSON, 50>;       // 还行
		//using Array = vector<JSON>;           // 还行
		using Object = InsertOrderMap<string, JSON>;

		// 第一个不使用，由于判断是否有值is_n_
		using Value = variant<void*, bool, Number, string, Array, Object>;
		// 类函数指针
		using FunPtr = void (JSON::*)(const char*&, const char*);

		// 考虑效率尽量不要使用拷贝移动构造赋值（编译器默认生成）
		JSON(const string& j);
		JSON(const char* p, const char* pend);
		JSON() {};
		~JSON() {};

		//JSON(const JSON& j) { if (!is_n_()) throw std::runtime_error("JSON can not copy!"); }
		JSON(const JSON& j) = delete;
		JSON(JSON&& j) = default;
		JSON& operator=(const JSON& j) = delete;
		JSON& operator=(JSON&& j) = default;
		void load(const string& file);

		// 支持从bool、Number、string赋值
		JSON& operator=(const bool& v) { v_ = v; return *this; }
		JSON& operator=(const Number& v) { v_ = v; return *this; }
		JSON& operator=(const long& v) { v_ = double(v); return *this; }
		JSON& operator=(const int& v) { v_ = double(v); return *this; }
		// 防止const char[N]重载到bool
		JSON& operator=(const char* v) { v_ = string(v); return *this; }
		JSON& operator=(const string& v) { v_ = v; return *this; }

		// 支持从带tie的类型赋值
		template<typename T>
		JSON& operator=(const T& t) {
			from_t_(t);
			return *this;
		}

		template<typename T>
		JSON& operator=(const vector<T>& v) {
			v_.emplace<Array>();

			for (auto& t : v) {
				push_back(t);
			}
			return *this;
		}

		// 支持从类似map类型赋值，下面enable_if_t排除匹配vector
		template<typename T, template<typename...> typename M>
		auto operator=(const M<string, T>& v) -> enable_if_t<!is_same_v<M<string, T>, vector<string>>, JSON&> {
			v_.emplace<Object>();

			auto& o = o_();
			for (auto& e : v) {
				o[e.first] = e.second;
			}

			return *this;
		}

		// 防止显式转换的问题，增加explicit，可以使用下面宏赋值
#define LB_JSON_EQ(v, j) (v = static_cast<decltype(v)>(j))

		// 支持赋值给bool、Number、string
		explicit operator bool() const { return get_b(); }
		explicit operator Number() const { return get_d(); }
		explicit operator long() const { return get_l(); }
		explicit operator int() const { return get_l(); }
		operator string() const { return get_s(); }
		operator string_view() const { return get_s(); }

		// 支持赋值给带tie的类型
		template<typename T>
		explicit operator T() const {
			T t;
			to_t_<T>(t);
			return t;
		}

		// 支持赋值给带vector
		template<typename T>
		vector<T> to_v_() const {
			vector<T> r;

			if (is_a_()) {
				r.resize(size());
				auto it = r.begin();

				for (const auto& e : *this) {
					*it++ = static_cast<T>(e);
				}
			}
			else if (is_o_()) {
				r.resize(size());
				auto it = r.begin();

				for (auto& e : items()) {
					*it++ = static_cast<T>(e.second);
				}
			}

			return r;
		}

		// 特例化，否则会同时匹配vector和M<string, T>
		operator vector<string>() const {
			return to_v_<string>();
		}

		template<typename T>
		operator vector<T>() const {
			return to_v_<T>();
		}

		// 支持赋值给map
		template<typename T, template<typename...> typename M>
		operator M<string, T>() const {
		//operator enable_if_t<!is_same_v<M<string, T>, vector<string>>, M<string, T>> () const {
			M<string, T> r;

			if (is_o_()) {
				for (auto& e : items()) {
					r[e.first] = static_cast<T>(e.second);
				}
			}

			return r;
		}

		void* get_v() const { return is_n_() ? n_() : nullptr; }
		long get_l(long def=0) const { return is_d_() ? (long)d_() : def; }
		long get_long(long def = 0) const { return get_l(def); }
		bool get_b(bool def=false) const { return is_b_() ? b_() : def; }
		bool get_bool(bool def = false) const { return get_b(def); }
		double get_d(double def=0.0) const { return is_d_() ? d_() : def; }
		double get_double(double def = 0.0) const { return get_d(def); }
		const string& get_s() const { return is_s_() ? s_() : s_s_; }
		const string& get_string() const { return get_s(); }
		string get_s(const string& def) const { return is_s_() ? s_() : def; }
		string get_string(const string& def) const { return get_s(def); }
private:
		Array& get_a() const { return is_a_() ? a_() : s_a_; }
public:
		bool contains(const string& key) const { return is_o_() ? o_().contains(key) : false; }
		size_t erase(const string& key) { return is_o_() ? o_().erase(key) : 0; }
		Object& items() const { return is_o_() ? o_() : s_o_; }

		// 按字符串取值
		// 1. 若为Object，则参数为key，否则为默认值
		string get_str(const string& key_or_def="") const;
		// 2. Array取值，出错给默认值
		string get_str(const size_t n, const string& def="") const;
		// 3. Object按key取值
		string get_str(const string& key, const string& def) const;

		// 大小
		size_t size() const {
			if (is_a_()) return a_().size();
			else if (is_o_()) return o_().size();
			return 0;
		}
		// 清除
		void clear() {
			if (is_a_()) v_.emplace<Array>();
			else if (is_o_()) v_.emplace<Object>();
		}
		// index
		size_t index() const { return v_.index(); }

		// 转为字符串
		template<bool h = false>
		void dump(string& s, size_t n = 1) const {
			if (is_d_()) s += doubleToString(d_());
			else if (is_b_()) s += (b_() ? "true" : "false");
			else if (is_s_()) dump_str(s_(), s);
			else if (is_n_()) s += "\"\"";
			else {
				if constexpr (h) {
					// 全是简单类型不缩进
					bool need = false;
					if (is_a_()) {
						for (auto& e : a_()) {
							if (!e.simple()) {
								need = true;
								break;
							}
						}
					}
					else {
						for (auto& e : o_()) {
							if (!e.second.simple()) {
								need = true;
								break;
							}
						}
					}

					if (!need) {
						dump(s);
						return;
					}
				}

				string head;
				char end;
				if constexpr (h) {
					head = "\n" + string(n, '\t');
				}

				if (is_a_()) {
					s += '[';
					end = ']';

					for (auto& e : a_()) {
						if constexpr (h) {
							s += head;
						}

						e.dump<h>(s, n + 1);
						s += ", ";
					}
				}
				else {
					s += '{';
					end = '}';

					for (auto& e : o_()) {
						if constexpr (h) {
							s += head;
						}

						dump_str(e.first, s);
						s += ": ";
						e.second.dump<h>(s, n + 1);
						s += ", ";
					}
				}

				if (size()) {
					s.pop_back();

					if constexpr (h) {
						s.pop_back();
						s += "\n" + string(n - 1, '\t');
						s += end;
					}
					else {
						s.back() = end;
					}
				}
				else {
					s += end;
				}
			}
		}

		template<bool h=false>
		string dump() const {
			string s;
			// 预留空间以加快速度
			s.reserve(1024 * 1024);
			dump<h>(s);
			return s;
		}

		// Array取值类似与string get(const size_t n, const string& def="")
		// 不过没有默认值，出错会有异常
		JSON& operator[] (const size_t n);

		// Array添加删除
		JSON& add_back() {
			if (!is_a_()) {
				v_.emplace<Array>();
			}

			return add_back_(a_());
		} 
		void pop_back() { a_().pop_back(); }
		template<typename T>
		void push_back(const T& v) { auto& a = add_back(); a = v; }

		// Object取值，没有会添加一个
		inline JSON& operator[] (const string& key) {
			if (!is_o_()) {
				v_.emplace<Object>();
			}

			return o_()[key];
		}

		// Object取值，没有会出错
		inline JSON& at(const string& key) const {
			if (!is_o_()) {
				cout << "JSON is not object!" << endl;
				throw std::runtime_error("JSON is not object!");
			}

			return o_().at(key);
		}

		// Array可以使用for in循环
		// Object可使用items()后for in循环
		auto begin() const { return get_a().begin(); }
		auto end() const { return get_a().end(); }

		friend ostream& operator<<(ostream& os, const JSON& j);
		friend istream& operator>>(istream& is, JSON& j);
		friend int inti_fun();

	private:
		// 类型不匹配时返回空Array或者Object，避免异常
		static string s_s_;
		static Array s_a_;
		static Object s_o_;

		HAS_MEMBER_FUNCTION(names);

		// 赋值给T
		template<typename T>
		void to_t_(T& t) const {
			if (is_a_()) {
				auto it = this->begin();
				auto end = this->end();

				auto fun = [&](auto&... members) {
					auto f = [&](auto& member) {
						if (it != end) {
							member = static_cast<remove_reference_t<decltype(member)>>(*it);
							++it;
						}
						};

					((f(members)), ...);
					};

				// 使用apply遍历tuple并赋值
				apply(fun, t.values());
			}
			else if (is_o_()) {
				auto& o = o_();

				// 有names就按名称取值，否则按顺序取值
				if constexpr (has_names<T>::value) {
					auto names = t.names();
					int index = 0;

					auto fun = [&](auto&... members) {
						auto f = [&](auto& member) {
							member = static_cast<remove_reference_t<decltype(member)>>(o[names[index++]]);
							};

						((f(members)), ...);
						};

					// 使用apply遍历tuple并赋值
					apply(fun, t.values());
				}
				else {
					auto it = o.begin();
					auto end = o.end();

					auto fun = [&](auto&... members) {
						auto f = [&](auto& member) {
							if (it != end) {
								member = static_cast<remove_reference_t<decltype(member)>>(it++->second);
							}
							};

						((f(members)), ...);
						};

					// 使用apply遍历tuple并赋值
					apply(fun, t.values());
				}
			}
		}

		// 从T获取值
		template<typename T>
		void from_t_(const T& t) {
			// 有names转为Object，否则为Array
			if constexpr (has_names<T>::value) {
				auto fun = [&](auto&... members) {
					auto names = t.names();
					int index = 0;

					auto f = [&](auto& member) {
						JSON& j = (*this)[names[index++]];
						j = member;
						};

					((f(members)), ...);
					};

				// 使用apply遍历tuple并赋值
				apply(fun, ((T&)t).values());
			}
			else {
				auto fun = [&](auto&... members) {
					auto f = [&](auto& member) {
						JSON& j = add_back();
						j = member;
						};

					((f(members)), ...);
					};

				// 使用apply遍历tuple并赋值
				apply(fun, ((T&)t).values());
			}
		}

		bool is_n_() const { return v_.index() == 0; }
		bool is_b_() const { return v_.index() == 1; }
		bool is_d_() const { return v_.index() == 2; }
		bool is_s_() const { return v_.index() == 3; }
		bool is_a_() const { return v_.index() == 4; }
		bool is_o_() const { return v_.index() == 5; }
		bool simple() const { return v_.index() < 4; }

		void*& n_() const { return (void*&)get<void*>(v_); }
		bool& b_() const { return (bool&)get<bool>(v_); }
		Number& d_() const { return (Number&)get<Number>(v_); }
		string& s_() const { return (string&)get<string>(v_); }
		Array& a_() const { return (Array&)get<Array>(v_); }
		Object& o_() const { return (Object&)get<Object>(v_); }

		// vector<JSON>没有add_back，使用if constexpr要求if和else都语法正确，只能使用模板匹配
		template<typename T>
		JSON& add_back_(T& t) {
			return t.add_back();
		}

		JSON& add_back_(vector<JSON>& t) {
			t.emplace_back();
			return t.back();
		}

		// 不需要使用if一个个判断每个字符怎么处理，应该可以加快速度
		static FunPtr fun_[256];

		void parse(const char* &p, const char*pend);
		void parse_end(const char* &p, const char* pend) { return; }
		void parse_quot(const char* &p, const char* pend);
		void parse_note(const char* &p, const char* pend);
		void parse_s(const char* &p, const char* pend);
		void parse_num(const char* &p, const char* pend);
		void parse_a(const char* &p, const char* pend);
		void parse_o(const char* &p, const char* pend);

		Value v_;
	};

	// 查找下一个字符
	inline const char* find_char(const char*p, const char* pend, const char*f) {
		return sz_find_byte(p, pend - p, f);
	}

	// 生产charset，不用每次查询都生产，到时使用静态变量
	inline sz_charset_t get_charset(const char* n, bool isnot = false) {
		//cout << n << " " << isnot << endl;
		sz_charset_t set;
		sz_charset_init(&set);
		
		size_t n_length = strlen(n);
		for (; n_length; ++n, --n_length) sz_charset_add(&set, *n);
		
		if (isnot) sz_charset_invert(&set);
		
		return set;
	}

	// 查找下一个非空字符
	// 下面所有find函数要求保证p < pend
	// 并且查询不到返回nullptr
	inline const char* find_next(const char*p, const char* pend) {
		static sz_charset_t set = get_charset(" \r\n\t,:", true);
		return sz_find_charset(p, pend - p, &set);
	}

	// 查找下一个\"，由于字符串处理
	inline const char* find_quot_end(const char* p, const char* pend) {
		static sz_charset_t set = get_charset("\\\"");
		return sz_find_charset(p, pend - p, &set);
	}

	// 查找下一个非数字，用于数值处理
	inline const char* find_num_end(const char* p, const char* pend) {
		static sz_charset_t set = get_charset("0123456789.eE+-", true);
		return sz_find_charset(p, pend - p, &set);
	}

	class Time {
	public:
		Time() { start_ = chrono::high_resolution_clock::now(); }
		~Time() {}

		void reset() { start_ = chrono::high_resolution_clock::now(); }

		long long get() const {
			auto end = chrono::high_resolution_clock::now();
			auto duration = chrono::duration_cast<chrono::microseconds>(end - start_);
			return duration.count();
		}

	private:
		decltype(chrono::high_resolution_clock::now()) start_;
	};

	HAS_MEMBER_FUNCTION(GetString);

	template<typename Container>
	string containerToString(const Container& cont, const string& delimiter = ", ") {
		ostringstream oss;
		auto it = cont.begin();

		if (it != cont.end()) {
			if constexpr (has_GetString<decltype(*it)>::value) {
				oss << it->GetString();
			}
			else {
				oss << *it;
			}
			++it;
		}

		for (; it != cont.end(); ++it) {
			if constexpr (has_GetString<decltype(*it)>::value) {
				oss << delimiter << it->GetString();
			}
			else {
				oss << delimiter << *it;
			}
		}

		return oss.str();
	}
}

