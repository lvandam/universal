#pragma once
// posit.hpp: definition of arbitrary posit number configurations
//
// Copyright (C) 2017 Stillwater Supercomputing, Inc.
//
// This file is part of the universal numbers project, which is released under an MIT Open Source license.

#include <cmath>
#include <cassert>
#include <iostream>
#include <limits>
#include <boost/multiprecision/cpp_dec_float.hpp>

using boost::multiprecision::cpp_dec_float_50;
using boost::multiprecision::cpp_dec_float_100;

// to yield a fast regression environment for productive development
// we want to leverage the IEEE floating point hardware available on x86 and ARM.
// Problem is that neither support a true IEEE 128bit long double.
// x86 provides a irreproducible x87 80bit format that is susceptible to inconsistent results due to multi-programming
// ARM only provides a 64bit double format.
// This conditional section is intended to create a unification of a long double format across
// different compilation environments that creates a fast verification environment through consistent hw support.
// Another option is to use a multiprecision floating point emulation layer.
// Side note: the performance of the bitset<> manipulation is slower than a multiprecision floating point implementation
// so this comment is talking about issues that will come to pass when we transition to a high performance sw emulation.

// 128bit double-double
struct __128bitdd {
	double upper;
	double lower;
};

#if defined(__clang__)
/* Clang/LLVM. ---------------------------------------------- */
typedef __128bitdd double_double;

#elif defined(__ICC) || defined(__INTEL_COMPILER)
/* Intel ICC/ICPC. ------------------------------------------ */
typedef __128bitdd double_double;

#elif defined(__GNUC__) || defined(__GNUG__)
/* GNU GCC/G++. --------------------------------------------- */
typedef __128bitdd double_double;

#elif defined(__HP_cc) || defined(__HP_aCC)
/* Hewlett-Packard C/aC++. ---------------------------------- */

#elif defined(__IBMC__) || defined(__IBMCPP__)
/* IBM XL C/C++. -------------------------------------------- */

#elif defined(_MSC_VER)
/* Microsoft Visual Studio. --------------------------------- */
typedef __128bitdd double_double;

#elif defined(__PGI)
/* Portland Group PGCC/PGCPP. ------------------------------- */

#elif defined(__SUNPRO_C) || defined(__SUNPRO_CC)
/* Oracle Solaris Studio. ----------------------------------- */

#endif

// Posits encode error conditions as NaR (Not a Real), propagating the error through arithmetic operations is preferred
#include "exceptions.hpp"
#include "../bitblock/bitblock.hpp"
#include "bit_functions.hpp"
#include "trace_constants.hpp"
#include "posit_functions.hpp"
#include "value.hpp"
#include "fraction.hpp"
#include "exponent.hpp"
#include "regime.hpp"

namespace sw {
	namespace unum {

// Forward definitions
template<size_t nbits, size_t es> class posit;
template<size_t nbits, size_t es> posit<nbits, es> abs(const posit<nbits, es>& p);
template<size_t nbits, size_t es> posit<nbits, es> sqrt(const posit<nbits, es>& p);
template<size_t nbits, size_t es> posit<nbits, es> minpos();
template<size_t nbits, size_t es> posit<nbits, es> maxpos();

// Not A Real is the posit encoding for INFINITY and arithmetic errors that can propagate
// The symbol NAR can be used to initialize a posit, i.e., posit<nbits,es>(NAR), or posit<nbits,es> p = NAR
#define NAR INFINITY

// define to non-zero if you want to enable arithmetic and logic literals
// POSIT_ENABLE_LITERALS

/*
 class posit represents arbitrary configuration posits and their basic arithmetic operations (add/sub, mul/div)
 */
template<size_t nbits, size_t es>
class posit {

	static_assert(es + 3 <= nbits, "Value for 'es' is too large for this 'nbits' value");
//	static_assert(sizeof(long double) == 16, "Posit library requires compiler support for 128 bit long double.");
//	static_assert((sizeof(long double) == 16) && (std::numeric_limits<long double>::digits < 113), "C++ math library for long double does not support 128-bit quad precision floats.");

	template <typename T>
	posit<nbits, es>& float_assign(const T& rhs) {
		constexpr int dfbits = std::numeric_limits<T>::digits - 1;
		value<dfbits> v((T)rhs);

		// special case processing
		if (v.isZero()) {
			setToZero();
			return *this;
		}
		if (v.isInfinite() || v.isNaN()) {  // posit encode for FP_INFINITE and NaN as NaR (Not a Real)
			setToNaR();
			return *this;
		}

		convert(v);
		return *this;
	}

public:
	static constexpr size_t rbits   = nbits - 1;
	static constexpr size_t ebits   = es;
	static constexpr size_t fbits   = nbits - 3 - es;
	static constexpr size_t abits   = fbits + 4;       // size of the addend
	static constexpr size_t fhbits  = fbits + 1;       // size of fraction + hidden bit
	static constexpr size_t mbits   = 2 * fhbits;      // size of the multiplier output
	static constexpr size_t divbits = 3 * fhbits + 4;  // size of the divider output

	posit() { setToZero();  }

	posit(const posit&) = default;
	posit(posit&&) = default;

	posit& operator=(const posit&) = default;
	posit& operator=(posit&&) = default;

	/// Construct posit from its components
	posit(bool sign, const regime<nbits, es>& r, const exponent<nbits, es>& e, const fraction<fbits>& f)
          : _sign(sign), _regime(r), _exponent(e), _fraction(f) {
		// generate raw bit representation
		_raw_bits = _sign ? twos_complement(collect()) : collect();
		_raw_bits.set(nbits - 1, _sign);
	}
	/// Construct posit from raw bits
	posit(const std::bitset<nbits>& raw_bits) {
		*this = set(raw_bits);
	}
	// initializers for native types
	posit(signed char initial_value)        { *this = initial_value; }
	posit(short initial_value)              { *this = initial_value; }
	posit(int initial_value)                { *this = initial_value; }
	posit(long long initial_value)          { *this = initial_value; }
	posit(unsigned long long initial_value) { *this = initial_value; }
	posit(float initial_value)              { *this = initial_value; }
	posit(double initial_value)             { *this = initial_value; }
	posit(long double initial_value)        { *this = initial_value; }
	// assignment operators for native types
	posit& operator=(signed char rhs) {
		value<8> v(rhs);
		if (v.isZero()) {
			setToZero();
			return *this;
		}
		else if (v.isNegative()) {
			convert(v);
			take_2s_complement();
		}
		else {
			convert(v);
		}
		return *this;
	}
	posit<nbits, es>& operator=(short rhs) {
		value<16> v(rhs);
		if (v.isZero()) {
			setToZero();
			return *this;
		}
		else if (v.isNegative()) {
			convert(v);
			take_2s_complement();
		}
		else {
			convert(v);
		}
		return *this;
	}
	posit<nbits, es>& operator=(int rhs) {
		value<32> v(rhs);
		if (v.isZero()) {
			setToZero();
			return *this;
		}
		else if (v.isNegative()) {
			convert(v);
			take_2s_complement();
		}
		else {
			convert(v);
		}
		return *this;
	}
	posit<nbits, es>& operator=(long long rhs) {
		value<64> v(rhs);
		if (v.isZero()) {
			setToZero();
			return *this;
		}
		else if (v.isNegative()) {
			convert(v);
			take_2s_complement();
		}
		else {
			convert(v);
		}
		return *this;
	}
	posit<nbits, es>& operator=(unsigned long long rhs) {
		value<64> v(rhs);
		if (v.isZero()) {
			setToZero();
			return *this;
		}
		else {
			convert(v);
		}
		convert(v);
		return *this;
	}
	posit<nbits, es>& operator=(float rhs) {
		return float_assign(rhs);
	}
	posit<nbits, es>& operator=(double rhs) {
		return float_assign(rhs);
	}
	posit<nbits, es>& operator=(long double rhs) {
       		return float_assign(rhs);
	}

	// prefix operator
	posit<nbits, es> operator-() const {
		if (isZero()) {
			return *this;
		}
		if (isNaR()) {
			return *this;
		}
		posit<nbits, es> negated;
		negated.decode(twos_complement(_raw_bits));
		return negated;
	}

	// we model a hw pipeline with register assignments, functional block, and conversion
	posit<nbits, es>& operator+=(const posit& rhs) {
		if (_trace_add) std::cout << "---------------------- ADD -------------------" << std::endl;
		// special case handling of the inputs
		if (isNaR() || rhs.isNaR()) {
			setToNaR();
			return *this;
		}
		if (isZero()) {
			*this = rhs;
			return *this;
		}
		if (rhs.isZero()) return *this;

		// arithmetic operation
		value<abits + 1> sum;
		value<fbits> a, b;
		// transform the inputs into (sign,scale,fraction) triples
		normalize(a);
		rhs.normalize(b);
		module_add<fbits,abits>(a, b, sum);		// add the two inputs

		// special case handling of the result
		if (sum.isZero()) {
			setToZero();
		}
		else if (sum.isInfinite()) {
			setToNaR();
		}
		else {
			convert(sum);
		}
		return *this;
	}
	posit<nbits, es>& operator+=(double rhs) {
		return *this += posit<nbits, es>(rhs);
	}
	posit<nbits, es>& operator-=(const posit& rhs) {
		if (_trace_sub) std::cout << "---------------------- SUB -------------------" << std::endl;
		// special case handling of the inputs
		if (isNaR() || rhs.isNaR()) {
			setToNaR();
			return *this;
		}
		if (isZero()) {
			*this = -rhs;
			return *this;
		}
		if (rhs.isZero()) return *this;

		// arithmetic operation
		value<abits + 1> difference;
		value<fbits> a, b;
		// transform the inputs into (sign,scale,fraction) triples
		normalize(a);
		rhs.normalize(b);
		module_subtract<fbits, abits>(a, b, difference);	// add the two inputs

		// special case handling of the result
		if (difference.isZero()) {
			setToZero();
		}
		else if (difference.isInfinite()) {
			setToNaR();
		}
		else {
			convert(difference);
		}
		return *this;
	}
	posit<nbits, es>& operator-=(double rhs) {
		return *this -= posit<nbits, es>(rhs);
	}
	posit<nbits, es>& operator*=(const posit& rhs) {
		static_assert(fhbits > 0, "posit configuration does not support multiplication");
		if (_trace_mul) std::cout << "---------------------- MUL -------------------" << std::endl;
		// special case handling of the inputs
		if (isNaR() || rhs.isNaR()) {
			setToNaR();
			return *this;
		}
		if (isZero() || rhs.isZero()) {
			setToZero();
			return *this;
		}

		// arithmetic operation
		value<mbits> product;
		value<fbits> a, b;
		// transform the inputs into (sign,scale,fraction) triples
		normalize(a);
		rhs.normalize(b);

		module_multiply(a, b, product);    // multiply the two inputs

		// special case handling on the output
		if (product.isZero()) {
			setToZero();
		}
		else if (product.isInfinite()) {
			setToNaR();
		}
		else {
			convert(product);
		}
		return *this;
	}
	posit<nbits, es>& operator*=(double rhs) {
		return *this *= posit<nbits, es>(rhs);
	}
	posit<nbits, es>& operator/=(const posit& rhs) {
		if (_trace_div) std::cout << "---------------------- DIV -------------------" << std::endl;
		// since we are encoding error conditions as NaR (Not a Real), we need to process that condition first
		if (rhs.isZero()) {
			setToNaR();
			return *this;
			//throw divide_by_zero{};    not throwing is a quiet signalling NaR
		}
		if (rhs.isNaR()) {
			setToNaR();
			return *this;
		}
		if (isZero() || isNaR()) {
			return *this;
		}

		value<divbits> ratio;
		value<fbits> a, b;
		// transform the inputs into (sign,scale,fraction) triples
		normalize(a);
		rhs.normalize(b);

		module_divide(a, b, ratio);

		// special case handling on the output
		if (ratio.isZero()) {
			throw "result can't be zero";
			setToZero();  // this can't happen as we would project back onto minpos
		}
		else if (ratio.isInfinite()) {
			throw "result can't be NaR";
			setToNaR();  // this can't happen as we would project back onto maxpos
		}
		else {
			convert<divbits>(ratio);
		}

		return *this;
	}
	posit<nbits, es>& operator/=(double rhs) {
		return *this /= posit<nbits, es>(rhs);
	}
	posit<nbits, es>& operator++() {
		increment_posit();
		return *this;
	}
	posit<nbits, es> operator++(int) {
		posit tmp(*this);
		operator++();
		return tmp;
	}
	posit<nbits, es>& operator--() {
		decrement_posit();
		return *this;
	}
	posit<nbits, es> operator--(int) {
		posit tmp(*this);
		operator--();
		return tmp;
	}

	posit<nbits, es> reciprocate() const {
		if (_trace_reciprocate) std::cout << "-------------------- RECIPROCATE ----------------" << std::endl;
		posit<nbits, es> p;
		// special case of NaR (Not a Real)
		if (isNaR()) {
			p.setToNaR();
			return p;
		}
		if (isZero()) {
			p.setToNaR();
			return p;
		}
		// compute the reciprocal
		bool old_sign = _sign;
		bitblock<nbits> raw_bits;
		if (isPowerOf2()) {
			raw_bits = twos_complement(_raw_bits);
			raw_bits.set(nbits-1, old_sign);
			p.set(raw_bits);
		}
		else {
			constexpr size_t operand_size = fhbits;
			bitblock<operand_size> one;
			one.set(operand_size - 1, true);
			bitblock<operand_size> frac;
			copy_into(_fraction.get(), 0, frac);
			frac.set(operand_size - 1, true);
			constexpr size_t reciprocal_size = 3 * fbits + 4;
			bitblock<reciprocal_size> reciprocal;
			divide_with_fraction(one, frac, reciprocal);
			if (_trace_reciprocate) {
				std::cout << "one    " << one << std::endl;
				std::cout << "frac   " << frac << std::endl;
				std::cout << "recip  " << reciprocal << std::endl;
			}

			// radix point falls at operand size == reciprocal_size - operand_size - 1
			reciprocal <<= operand_size - 1;
			if (_trace_reciprocate) std::cout << "frac   " << reciprocal << std::endl;
			int new_scale = -scale();
			int msb = findMostSignificantBit(reciprocal);
			if (msb > 0) {
				int shift = reciprocal_size - msb;
				reciprocal <<= shift;
				new_scale -= (shift-1);
				if (_trace_reciprocate) std::cout << "result " << reciprocal << std::endl;
			}
			//std::bitset<operand_size> tr;
			//truncate(reciprocal, tr);
			//std::cout << "tr     " << tr << std::endl;
			p.convert(_sign, new_scale, reciprocal);
		}
		return p;
	}
	// SELECTORS
	bool isNaR() const {
		return (_sign & _regime.isZero());
	}
	bool isZero() const {
		return (!_sign & _regime.isZero());
	}
	bool isOne() const { // pattern 010000....
		bitblock<nbits> tmp(_raw_bits);
		tmp.set(nbits - 2, false);
		bool oneBitSet = tmp.none();
		return !_sign & oneBitSet;
	}
	bool isMinusOne() const { // pattern 110000...
		bitblock<nbits> tmp(_raw_bits);
		tmp.set(nbits - 1, false);
		tmp.set(nbits - 2, false);
		bool oneBitSet = tmp.none();
		return _sign & oneBitSet;
	}
	bool isNegative() const {
		return _sign;
	}
	bool isPositive() const {
		return !_sign;
	}
	bool isPowerOf2() const {
		return _fraction.none();
	}

	inline int	      sign_value() const {
		return (_sign ? -1 : 1);
	}
	inline double   regime_value() const {
		return _regime.value();
	}
	inline double exponent_value() const {
		return _exponent.value();
	}
	inline double fraction_value() const {
		return _fraction.value();
	}

	int				   regime_k() const {
		return _regime.regime_k();
	}
	int                get_scale() const { return _regime.scale() + _exponent.scale(); }
	bool               get_sign() const { return _sign;  }
	regime<nbits, es>  get_regime() const {
		return _regime;
	}
	exponent<nbits,es> get_exponent() const {
		return _exponent;
	}
	fraction<fbits>    get_fraction() const {
		return _fraction;
	}
	bitblock<nbits>    get() const {
		return _raw_bits;
	}
	bitblock<nbits>    get_decoded() const {
		bitblock<rbits> r = _regime.get();
		size_t nrRegimeBits = _regime.nrBits();
		bitblock<es> e = _exponent.get();
		size_t nrExponentBits = _exponent.nrBits();
		bitblock<fbits> f = _fraction.get();
		size_t nrFractionBits = _fraction.nrBits();

		bitblock<nbits> _Bits;
		_Bits.set(nbits - 1, _sign);
		int msb = nbits - 2;
		for (size_t i = 0; i < nrRegimeBits; i++) {
			_Bits.set(std::size_t(msb--), r[nbits - 2 - i]);
		}
		if (msb < 0)
                    return _Bits;
		for (size_t i = 0; i < nrExponentBits && msb >= 0; i++) {
			_Bits.set(std::size_t(msb--), e[es - 1 - i]);
		}
		if (msb < 0) return _Bits;
		for (size_t i = 0; i < nrFractionBits && msb >= 0; i++) {
			_Bits.set(std::size_t(msb--), f[fbits - 1 - i]);
		}
		return _Bits;
	}
	std::string        get_quadrant() const {
		posit<nbits, es> pOne(1), pMinusOne(-1);
		if (_sign) {
			// west
			if (*this > pMinusOne) {
				return "SW";
			}
			else {
				return "NW";
			}
		}
		else {
			// east
			if (*this < pOne) {
				return "SE";
			}
			else {
				return "NE";
			}
		}
	}
	long long          get_encoding_as_integer() const {
		if (nbits > 64) throw "encoding cannot be represented by a 64bit integer";
		long long as_integer = 0;
		unsigned long long mask = 1;
		for (unsigned i = 0; i < nbits; i++) {
			if (_raw_bits[i]) as_integer |= mask;
			mask <<= 1;
		}
		return as_integer;
	}
	// MODIFIERS
	inline void clear() {
		_sign = false;
		_regime.reset();
		_exponent.reset();
		_fraction.reset();
		_raw_bits.reset();
	}
	inline void setToZero() {
		_sign = false;
		_regime.setToZero();
		_exponent.reset();
		_fraction.reset();
		_raw_bits.reset();
	}
	inline void setToNaR() {
		_sign = true;
		_regime.setToInfinite();
		_exponent.reset();
		_fraction.reset();
		_raw_bits.reset();
		_raw_bits.set(nbits - 1, true);
	}
	posit<nbits, es>& set(const bitblock<nbits>& raw_bits) {
		decode(raw_bits);
		return *this;
	}
	// Set the raw bits of the posit given a binary pattern
	posit<nbits,es>& set_raw_bits(uint64_t value) {
		clear();
		bitblock<nbits> raw_bits;
		uint64_t mask = 1;
		for ( int i = 0; i < nbits; i++ ) {
			raw_bits.set(i,(value & mask));
			mask <<= 1;
		}
		// decode to cache the posit number interpretation
		decode(raw_bits);
		return *this;
	}
	int decode_regime(bitblock<nbits>& raw_bits) {
		// let m be the number of identical bits in the regime
		int m = 0;   // regime runlength counter
		int k = 0;   // converted regime scale
		if (raw_bits[nbits - 2] == 1) {   // run length of 1's
			m = 1;   // if a run of 1's k = m - 1
			for (int i = nbits - 3; i >= 0; --i) {
				if (raw_bits[i] == 1) {
					m++;
				}
				else {
					break;
				}
			}
			k = m - 1;
		}
		else {
			m = 1;  // if a run of 0's k = -m
			for (int i = nbits - 3; i >= 0; --i) {
				if (raw_bits[i] == 0) {
					m++;
				}
				else {
					break;
				}
			}
			k = -m;
		}
		return k;
	}
	// decode takes the raw bits representing a posit coming from memory
	// and decodes the regime, the exponent, and the fraction.
	// This function has the functionality of the posit register-file load.
	void extract_fields(const bitblock<nbits>& raw_bits) {
		bitblock<nbits> tmp(raw_bits);
		if (_sign) tmp = twos_complement(tmp);
		size_t nrRegimeBits = _regime.assign_regime_pattern(decode_regime(tmp));

		// get the exponent bits
		// start of exponent is nbits - (sign_bit + regime_bits)
		int msb = int(int(nbits) - 1 - (1 + nrRegimeBits));
		size_t nrExponentBits = 0;
		if (es > 0) {
			bitblock<es> _exp;
			if (msb >= 0 && es > 0) {
				nrExponentBits = (msb >= es - 1 ? es : msb + 1);
				for (size_t i = 0; i < nrExponentBits; i++) {
					_exp[es - 1 - i] = tmp[msb - i];
				}
			}
			_exponent.set(_exp, nrExponentBits);
		}

		// finally, set the fraction bits
		// we do this so that the fraction is right extended with 0;
		// The max fraction is <nbits - 3 - es>, but we are setting it to <nbits - 3> and right-extent
		// The msb bit of the fraction represents 2^-1, the next 2^-2, etc.
		// If the fraction is empty, we have a fraction of nbits-3 0 bits
		// If the fraction is one bit, we have still have fraction of nbits-3, with the msb representing 2^-1, and the rest are right extended 0's
		bitblock<fbits> _frac;
		msb = msb - int(nrExponentBits);
		size_t nrFractionBits = (msb < 0 ? 0 : msb + 1);
		if (msb >= 0) {
			for (int i = msb; i >= 0; --i) {
				_frac[fbits - 1 - (msb - i)] = tmp[i];
			}
		}
		_fraction.set(_frac, nrFractionBits);
	}
	void decode(const bitblock<nbits>& raw_bits) {
		_raw_bits = raw_bits;	// store the raw bits for reference
		// check special cases
		_sign     = raw_bits.test(nbits - 1);
		// check for special cases
		bool special = false;
		if (_sign) {
			std::bitset<nbits> tmp(raw_bits);
			tmp.reset(nbits - 1);
			if (tmp.none()) {
				setToNaR();  // special case = NaR (Not a Real)
			}
			else {
				extract_fields(raw_bits);
			}
		}
		else {
			if (raw_bits.none()) {  // special case = 0
				setToZero();
			}
			else {
				extract_fields(raw_bits);
			}
		}
		if (_trace_decode) std::cout << "raw bits: " << _raw_bits << " posit bits: " << (_sign ? "1|" : "0|") << _regime << "|" << _exponent << "|" << _fraction << " posit value: " << *this << std::endl;

		// we are storing both the raw bit representation and the decoded form
		// so no need to transform back via 2's complement of regime/exponent/fraction
	}



	// Maybe remove explicit, MTL compiles, but we have lots of double computation then
	explicit operator cpp_dec_float_50() const { return to_cpp_dec_float_50(); }
	explicit operator cpp_dec_float_100() const { return to_cpp_dec_float_100(); }
	explicit operator long double() const { return to_long_double(); }
	explicit operator double() const { return to_double(); }
	explicit operator float() const { return to_float(); }
	explicit operator long long() const { return to_long_long(); }
	explicit operator long() const { return to_long(); }
	explicit operator int() const { return to_int(); }

	// currently, size is tied to fbits size of posit config. Is there a need for a case that captures a user-defined sized fraction?
	value<fbits> convert_to_scientific_notation() const {
		return value<fbits>(_sign, scale(), get_fraction().get(), isZero(), isNaR());
	}
	value<fbits> to_value() const {
		return value<fbits>(_sign, scale(), get_fraction().get(), isZero(), isNaR());
	}
	void normalize(value<fbits>& v) const {
		v.set(_sign, scale(), _fraction.get(), isZero(), isNaR());
	}
	template<size_t tgt_fbits>
	void normalize_to(value<tgt_fbits>& v) const {
		bitblock<tgt_fbits> _fr;
		bitblock<fbits> _src = _fraction.get();
		int tgt, src;
		for (tgt = int(tgt_fbits) - 1, src = int(fbits) - 1; tgt >= 0, src >= 0; tgt--, src--) _fr[tgt] = _src[src];
		v.set(_sign, scale(), _fr, isZero(), isNaR());
	}
	// collect the posit components into a bitset
	bitblock<nbits> collect() {
		bitblock<rbits> r = _regime.get();
		size_t nrRegimeBits = _regime.nrBits();
		bitblock<es> e = _exponent.get();
		size_t nrExponentBits = _exponent.nrBits();
		bitblock<fbits> f = _fraction.get();
		size_t nrFractionBits = _fraction.nrBits();
		bitblock<nbits> raw_bits;
		// collect
		raw_bits.set(nbits - 1, _sign);
		int msb = int(nbits) - 2;
		for (size_t i = 0; i < nrRegimeBits; i++) {
			raw_bits.set(msb--, r[nbits - 2 - i]);
		}
		if (msb >= 0) {
			for (size_t i = 0; i < nrExponentBits; i++) {
				raw_bits.set(msb--, e[es - 1 - i]);
			}
		}
		if (msb >= 0) {
			for (size_t i = 0; i < nrFractionBits; i++) {
				raw_bits.set(msb--, f[fbits - 1 - i]);
			}
		}
		return raw_bits;
	}
	// given a decoded posit, take its 2's complement
	void take_2s_complement() {
		// transform back through 2's complement
		bitblock<rbits> r = _regime.get();
		size_t nrRegimeBits = _regime.nrBits();
		bitblock<es> e = _exponent.get();
		size_t nrExponentBits = _exponent.nrBits();
		bitblock<fbits> f = _fraction.get();
		size_t nrFractionBits = _fraction.nrBits();
		bitblock<nbits> raw_bits;
		// collect
		raw_bits.set(int(nbits) - 1, _sign);
		int msb = int(nbits) - 2;
		for (size_t i = 0; i < nrRegimeBits; i++) {
			raw_bits.set(msb--, r[nbits - 2 - i]);
		}
		if (msb >= 0) {
			for (size_t i = 0; i < nrExponentBits; i++) {
				raw_bits.set(msb--, e[es - 1 - i]);
			}
		}
		if (msb >= 0) {
			for (size_t i = 0; i < nrFractionBits; i++) {
				raw_bits.set(msb--, f[fbits - 1 - i]);
			}
		}
		// transform
		raw_bits = twos_complement(raw_bits);
		// distribute
		bitblock<nbits - 1> regime_bits;
		for (unsigned int i = 0; i < nrRegimeBits; i++) {
			regime_bits.set(nbits - 2 - i, raw_bits[nbits - 2 - i]);
		}
		_regime.set(regime_bits, nrRegimeBits);
		if (es > 0 && nrExponentBits > 0) {
			bitblock<es> exponent_bits;
			for (size_t i = 0; i < nrExponentBits; i++) {
				exponent_bits.set(es - 1 - i, raw_bits[nbits - 2 - nrRegimeBits - i]);
			}
			_exponent.set(exponent_bits, nrExponentBits);
		}
		if (nrFractionBits > 0) {
			bitblock<fbits> fraction_bits;   // was nbits - 2
			for (size_t i = 0; i < nrFractionBits; i++) {
				// fraction_bits.set(nbits - 3 - i, raw_bits[nbits - 2 - nrRegimeBits - nrExponentBits - i]);
				fraction_bits.set(fbits - 1 - i, raw_bits[nbits - 2 - nrRegimeBits - nrExponentBits - i]);
			}
			_fraction.set(fraction_bits, nrFractionBits);
		}
	}
	// scale returns the shifts to normalize the number =  regime + exponent shifts
	int scale() const {
		// how many shifts represent the regime?
		// regime = useed ^ k = 2 ^ (k*(2 ^ e))
		// scale = useed ^ k * 2^e
		return _regime.scale() + _exponent.scale();
	}
	unsigned int exp() const {
		return _exponent.scale();
	}
	// special case check for projecting values between (0, minpos] to minpos and [maxpos, inf) to maxpos
	// Returns true if the scale is too small or too large for this posit config
	// DO NOT USE the k value for this, as the k value encodes the useed regions and thus is too coarse to make this decision
	// Using the scale directly is the simplest expression of the inward projection test.
	bool check_inward_projection_range(int scale) {
		// calculate the max k factor for this posit config
		int posit_size = nbits;
		int k = scale < 0 ?	-(posit_size - 2) : (posit_size - 2);
		return scale < 0 ? scale < k*(1<<es) : scale > k*(1<<es);
	}
	// project to the next 'larger' posit: this is 'pushing away' from zero, projecting to the next bigger scale
	void project_up() {
		bool carry = _fraction.increment();
		if (carry && es > 0)
			carry = _exponent.increment();
		if (carry)
                    _regime.increment();
	}
	// step up to the next posit in a lexicographical order
	void increment_posit() {
		bitblock<nbits> raw(_raw_bits);
		increment_bitset(raw);
		decode(raw);
	}
	// step down to the previous posit in a lexicographical order
	void decrement_posit() {
		bitblock<nbits> raw(_raw_bits);
		decrement_bitset(raw);
		decode(raw);
	}

	// Generalized version
	template <size_t FBits>
	inline void convert(const value<FBits>& v) {
		if (v.isZero()) {
			setToZero();
			return;
		}
		if (v.isNaN() || v.isInfinite()) {
			setToNaR();
			return;
		}
		convert(v.sign(), v.scale(), v.fraction());
    }
	// convert assumes that ZERO and NaR cases are handled. Only non-zero and non-NaR values are allowed.
	template<size_t input_fbits>
	void convert(bool sign, int scale, bitblock<input_fbits> input_fraction) {
		clear();
		if (_trace_conversion) std::cout << "------------------- CONVERT ------------------" << std::endl;
		if (_trace_conversion) std::cout << "sign " << (sign ? "-1 " : " 1 ") << "scale " << std::setw(3) << scale << " fraction " << input_fraction << std::endl;

		// construct the posit
		_sign = sign;
		int k = calculate_unconstrained_k<nbits, es>(scale);
		// interpolation rule checks
		if (check_inward_projection_range(scale)) {    // regime dominated
			if (_trace_conversion) std::cout << "inward projection" << std::endl;
			// we are projecting to minpos/maxpos
			_regime.assign_regime_pattern(k);
			// store raw bit representation
			_raw_bits = _sign ? twos_complement(collect()) : collect();
			_raw_bits.set(nbits - 1, _sign);
			// we are done
			if (_trace_rounding) std::cout << "projection  rounding ";
		}
		else {
			const size_t pt_len = nbits + 3 + es;
			bitblock<pt_len> pt_bits;
			bitblock<pt_len> regime;
			bitblock<pt_len> exponent;
			bitblock<pt_len> fraction;
			bitblock<pt_len> sticky_bit;

			bool s = sign;
			int e = scale;
			bool r = (e >= 0);

			unsigned run = (r ? 1 + (e >> es) : -(e >> es));
			regime.set(0, 1 ^ r);
			for (unsigned i = 1; i <= run; i++) regime.set(i, r);

			unsigned esval = e % (uint32_t(1) << es);
			exponent = convert_to_bitblock<pt_len>(esval);
			unsigned nf = (unsigned)std::max<int>(0, (nbits + 1) - (2 + run + es));
			// TODO: what needs to be done if nf > fbits?
			//assert(nf <= input_fbits);
			// copy the most significant nf fraction bits into fraction
			unsigned lsb = nf <= input_fbits ? 0 : nf - input_fbits;
			for (unsigned i = lsb; i < nf; i++) fraction[i] = input_fraction[input_fbits - nf + i];

			bool sb = anyAfter(input_fraction, input_fbits - 1 - nf);

			// construct the untruncated posit
			// pt    = BitOr[BitShiftLeft[reg, es + nf + 1], BitShiftLeft[esval, nf + 1], BitShiftLeft[fv, 1], sb];
			regime <<= es + nf + 1;
			exponent <<= nf + 1;
			fraction <<= 1;
			sticky_bit.set(0, sb);

			pt_bits |= regime;
			pt_bits |= exponent;
			pt_bits |= fraction;
			pt_bits |= sticky_bit;

			unsigned len = 1 + std::max<unsigned>((nbits + 1), (2 + run + es));
			bool blast = pt_bits.test(len - nbits);
			bool bafter = pt_bits.test(len - nbits - 1);
			bool bsticky = anyAfter(pt_bits, len - nbits - 1 - 1);

			bool rb = (blast & bafter) | (bafter & bsticky);

			pt_bits <<= pt_len - len;
			bitblock<nbits> ptt;
			truncate(pt_bits, ptt);

			if (rb) increment_bitset(ptt);
			if (s) ptt = twos_complement(ptt);
			decode(ptt);
		}
	}

private:
	bitblock<nbits>        _raw_bits;	// raw bit representation
	bool				   _sign;       // decoded posit representation
	regime<nbits, es>	   _regime;		// decoded posit representation
	exponent<nbits, es>    _exponent;	// decoded posit representation
	fraction<fbits> 	   _fraction;	// decoded posit representation

	// HELPER methods
	// Conversion functions
	int         to_int() const {
		if (isZero()) return 0;
		if (isNaR()) throw "NaR (Not a Real)";
		return int(to_float());
	}
	long        to_long() const {
		if (isZero()) return 0;
		if (isNaR()) throw "NaR (Not a Real)";
		return long(to_double());
	}
	long long   to_long_long() const {
		if (isZero()) return 0;
		if (isNaR()) throw "NaR (Not a Real)";
		return long(to_long_double());
	}
	float       to_float() const {
		return (float)to_double();
	}
	double      to_double() const {
		if (isZero())	return 0.0;
		if (isNaR())	return NAN;
		return sign_value() * regime_value() * exponent_value() * (1.0 + fraction_value());
	}
	long double to_long_double() const {
		if (isZero())  return 0.0;
		if (isNaR())   return NAN;
		int s = sign_value();
		double r = regime_value(); // regime value itself will fit in a double
		double e = exponent_value(); // same with exponent
		long double f = (long double)(1.0) + _fraction.value();
		return s * r * e * f;
	}
	cpp_dec_float_50 to_cpp_dec_float_50() const {
		if (isZero())  return 0.0;
		if (isNaR())   return NAN;
		int s = sign_value();
		cpp_dec_float_50 r = regime_value(); // regime value itself will fit in a double
		cpp_dec_float_50 e = exponent_value(); // same with exponent
		cpp_dec_float_50 f = (cpp_dec_float_50)(1.0) + _fraction.value();
		return s * r * e * f;
	}
	cpp_dec_float_100 to_cpp_dec_float_100() const {
		if (isZero())  return 0.0;
		if (isNaR())   return NAN;
		int s = sign_value();
		cpp_dec_float_100 r = regime_value(); // regime value itself will fit in a double
		cpp_dec_float_100 e = exponent_value(); // same with exponent
		cpp_dec_float_100 f = (cpp_dec_float_100)(1.0) + _fraction.value();
		return s * r * e * f;
	}

	// friend functions
    // template parameters need names different from class template parameters (for gcc and clang)
	template<size_t nnbits, size_t ees>
	friend std::ostream& operator<< (std::ostream& ostr, const posit<nnbits, ees>& p);
	template<size_t nnbits, size_t ees>
	friend std::istream& operator>> (std::istream& istr, posit<nnbits, ees>& p);

	// posit - posit logic functions
	template<size_t nnbits, size_t ees>
	friend bool operator==(const posit<nnbits, ees>& lhs, const posit<nnbits, ees>& rhs);
	template<size_t nnbits, size_t ees>
	friend bool operator!=(const posit<nnbits, ees>& lhs, const posit<nnbits, ees>& rhs);
	template<size_t nnbits, size_t ees>
	friend bool operator< (const posit<nnbits, ees>& lhs, const posit<nnbits, ees>& rhs);
	template<size_t nnbits, size_t ees>
	friend bool operator> (const posit<nnbits, ees>& lhs, const posit<nnbits, ees>& rhs);
	template<size_t nnbits, size_t ees>
	friend bool operator<=(const posit<nnbits, ees>& lhs, const posit<nnbits, ees>& rhs);
	template<size_t nnbits, size_t ees>
	friend bool operator>=(const posit<nnbits, ees>& lhs, const posit<nnbits, ees>& rhs);

#if POSIT_ENABLE_LITERALS
	// posit - literal logic functions
	// posit - int
	template<size_t nnbits, size_t ees>
	friend bool operator==(const posit<nnbits, ees>& lhs, int rhs);
	template<size_t nnbits, size_t ees>
	friend bool operator!=(const posit<nnbits, ees>& lhs, int rhs);
	template<size_t nnbits, size_t ees>
	friend bool operator< (const posit<nnbits, ees>& lhs, int rhs);
	template<size_t nnbits, size_t ees>
	friend bool operator> (const posit<nnbits, ees>& lhs, int rhs);
	template<size_t nnbits, size_t ees>
	friend bool operator<=(const posit<nnbits, ees>& lhs, int rhs);
	template<size_t nnbits, size_t ees>
	friend bool operator>=(const posit<nnbits, ees>& lhs, int rhs);
	// posit - double
	template<size_t nnbits, size_t ees>
	friend bool operator==(const posit<nnbits, ees>& lhs, double rhs);
	template<size_t nnbits, size_t ees>
	friend bool operator!=(const posit<nnbits, ees>& lhs, double rhs);
	template<size_t nnbits, size_t ees>
	friend bool operator< (const posit<nnbits, ees>& lhs, double rhs);
	template<size_t nnbits, size_t ees>
	friend bool operator> (const posit<nnbits, ees>& lhs, double rhs);
	template<size_t nnbits, size_t ees>
	friend bool operator<=(const posit<nnbits, ees>& lhs, double rhs);
	template<size_t nnbits, size_t ees>
	friend bool operator>=(const posit<nnbits, ees>& lhs, double rhs);
    // posit - long double
    template<size_t nnbits, size_t ees>
    friend bool operator> (long double lhs, const posit<nnbits, ees>& rhs);
#endif // POSIT_ENABLE_LITERALS

};

////////////////// POSIT operators

// stream operators
template<size_t nbits, size_t es>
inline std::ostream& operator<<(std::ostream& ostr, const posit<nbits, es>& p) {
	if (p.isZero()) {
		ostr << double(0.0);
		return ostr;
	}
	else if (p.isNaR()) {
		ostr << "NaR";
		return ostr;
	}
	ostr << p.to_double();
	return ostr;
}

// TODO: this needs an implementation
template<size_t nbits, size_t es>
inline std::istream& operator>> (std::istream& istr, const posit<nbits, es>& p) {
	istr >> p._Bits;
	return istr;
}

// posit - posit binary logic operators
template<size_t nbits, size_t es>
inline bool operator==(const posit<nbits, es>& lhs, const posit<nbits, es>& rhs) {
	return lhs._raw_bits == rhs._raw_bits;
}
template<size_t nbits, size_t es>
inline bool operator!=(const posit<nbits, es>& lhs, const posit<nbits, es>& rhs) {
	return !operator==(lhs, rhs);
}
template<size_t nbits, size_t es>
inline bool operator< (const posit<nbits, es>& lhs, const posit<nbits, es>& rhs) {
	return lessThan(lhs._raw_bits, rhs._raw_bits);
}
template<size_t nbits, size_t es>
inline bool operator> (const posit<nbits, es>& lhs, const posit<nbits, es>& rhs) {
	return operator< (rhs, lhs);
}
template<size_t nbits, size_t es>
inline bool operator<=(const posit<nbits, es>& lhs, const posit<nbits, es>& rhs) {
	return operator< (lhs, rhs) || operator==(lhs, rhs);
}
template<size_t nbits, size_t es>
inline bool operator>=(const posit<nbits, es>& lhs, const posit<nbits, es>& rhs) {
	return !operator< (lhs, rhs);
}
template<size_t nbits, size_t es>
inline bool operator> (long double lhs, const posit<nbits, es>& rhs) {
    return operator< (rhs, posit<nbits, es>(lhs));
}

// posit - posit binary arithmetic operators
// BINARY ADDITION
template<size_t nbits, size_t es>
inline posit<nbits, es> operator+(const posit<nbits, es>& lhs, const posit<nbits, es>& rhs) {
	posit<nbits, es> sum = lhs;
	sum += rhs;
	return sum;
}
// BINARY SUBTRACTION
template<size_t nbits, size_t es>
inline posit<nbits, es> operator-(const posit<nbits, es>& lhs, const posit<nbits, es>& rhs) {
	posit<nbits, es> diff = lhs;
	diff -= rhs;
	return diff;
}
// BINARY MULTIPLICATION
template<size_t nbits, size_t es>
inline posit<nbits, es> operator*(const posit<nbits, es>& lhs, const posit<nbits, es>& rhs) {
	posit<nbits, es> mul = lhs;
	mul *= rhs;
	return mul;
}
// BINARY DIVISION
template<size_t nbits, size_t es>
inline posit<nbits, es> operator/(const posit<nbits, es>& lhs, const posit<nbits, es>& rhs) {
	posit<nbits, es> ratio = lhs;
	ratio /= rhs;
	return ratio;
}

#if POSIT_ENABLE_LITERALS

// posit - literal int logic operators
template<size_t nbits, size_t es>
inline bool operator==(const posit<nbits, es>& lhs, int rhs) {
	return lhs == posit<nbits, es>(rhs);
}
template<size_t nbits, size_t es>
inline bool operator!=(const posit<nbits, es>& lhs, int rhs) {
	return !operator==(lhs, posit<nbits, es>(rhs));
}
template<size_t nbits, size_t es>
inline bool operator< (const posit<nbits, es>& lhs, int rhs) {
	return lessThan(lhs._raw_bits, posit<nbits, es>(rhs)._raw_bits);
}
template<size_t nbits, size_t es>
inline bool operator> (const posit<nbits, es>& lhs, int rhs) {
	return operator< (posit<nbits, es>(rhs), lhs);
}
template<size_t nbits, size_t es>
inline bool operator<=(const posit<nbits, es>& lhs, int rhs) {
	return operator< (lhs, posit<nbits, es>(rhs)) || operator==(lhs, posit<nbits, es>(rhs));
}
template<size_t nbits, size_t es>
inline bool operator>=(const posit<nbits, es>& lhs, int rhs) {
	return !operator<(lhs, posit<nbits, es>(rhs));
}

// posit - literal double logic operators
template<size_t nbits, size_t es>
inline bool operator==(const posit<nbits, es>& lhs, double rhs) {
	return lhs == posit<nbits, es>(rhs);
}
template<size_t nbits, size_t es>
inline bool operator!=(const posit<nbits, es>& lhs, double rhs) {
	return !operator==(lhs, posit<nbits, es>(rhs));
}
template<size_t nbits, size_t es>
inline bool operator< (const posit<nbits, es>& lhs, double rhs) {
	return lessThan(lhs._raw_bits, posit<nbits, es>(rhs)._raw_bits);
}
template<size_t nbits, size_t es>
inline bool operator> (const posit<nbits, es>& lhs, double rhs) {
	return operator< (posit<nbits, es>(rhs), lhs);
}
template<size_t nbits, size_t es>
inline bool operator<=(const posit<nbits, es>& lhs, double rhs) {
	return operator< (lhs, posit<nbits, es>(rhs)) || operator==(lhs, posit<nbits, es>(rhs));
}
template<size_t nbits, size_t es>
inline bool operator>=(const posit<nbits, es>& lhs, double rhs) {
	return !operator<(lhs, posit<nbits, es>(rhs));
}

// BINARY ADDITION
template<size_t nbits, size_t es>
inline posit<nbits, es> operator+(const posit<nbits, es>& lhs, double rhs) {
	posit<nbits, es> sum = lhs;
	sum += rhs;
	return sum;
}
template<size_t nbits, size_t es>
inline posit<nbits, es> operator+(double lhs, const posit<nbits, es>& rhs) {
	posit<nbits, es> sum = lhs;
	sum += rhs;
	return sum;
}

// BINARY SUBTRACTION
template<size_t nbits, size_t es>
inline posit<nbits, es> operator-(double lhs, const posit<nbits, es>& rhs) {
	posit<nbits, es> sum = lhs;
	sum -= rhs;
	return sum;
}
template<size_t nbits, size_t es>
inline posit<nbits, es> operator-(const posit<nbits, es>& lhs, double rhs) {
	posit<nbits, es> diff = lhs;
	diff -= rhs;
	return diff;
}
// BINARY MULTIPLICATION
template<size_t nbits, size_t es>
inline posit<nbits, es> operator*(double lhs, const posit<nbits, es>& rhs) {
	posit<nbits, es> sum = lhs;
	sum *= rhs;
	return sum;
}
template<size_t nbits, size_t es>
inline posit<nbits, es> operator*(const posit<nbits, es>& lhs, double rhs) {
	posit<nbits, es> mul = lhs;
	mul *= rhs;
	return mul;
}
// BINARY DIVISION
template<size_t nbits, size_t es>
inline posit<nbits, es> operator/(double lhs, const posit<nbits, es>& rhs) {
	posit<nbits, es> sum = lhs;
	sum /= rhs;
	return sum;
}
template<size_t nbits, size_t es>
inline posit<nbits, es> operator/(const posit<nbits, es>& lhs, double rhs) {
	posit<nbits, es> ratio = lhs;
	ratio /= rhs;
	return ratio;
}

#endif // POSIT_ENABLE_LITERALS

// Magnitude of a posit (equivalent to turning the sign bit off).
template<size_t nbits, size_t es>
posit<nbits, es> abs(const posit<nbits, es>& p) {
    return posit<nbits, es>(false, p.get_regime(), p.get_exponent(), p.get_fraction());
}

// generate a posit representing minpos
template<size_t nbits, size_t es>
posit<nbits, es> minpos() {
	posit<nbits, es> p;
	p++;
	return p;
}

// generate a posit representing maxpos
template<size_t nbits, size_t es>
posit<nbits, es> maxpos() {
	posit<nbits, es> p;
	p.setToNaR();
	--p;
	return p;
}

// QUIRE OPERATORS
// Why are they defined here and not in quire.hpp? TODO

template<size_t nbits, size_t es>
value<nbits - es + 2> quire_add(const posit<nbits, es>& lhs, const posit<nbits, es>& rhs) {
	static constexpr size_t fbits = nbits - 3 - es;
	static constexpr size_t abits = fbits + 4;       // size of the addend
	value<abits + 1> sum;
	value<fbits> a, b;

	if (lhs.isZero() && rhs.isZero()) return sum;

	// transform the inputs into (sign,scale,fraction) triples
	a.set(lhs.get_sign(), lhs.scale(), lhs.get_fraction().get(), lhs.isZero(), lhs.isNaR());;
	b.set(rhs.get_sign(), rhs.scale(), rhs.get_fraction().get(), rhs.isZero(), rhs.isNaR());;

	module_add<fbits, abits>(a, b, sum);		// add the two inputs

	return sum;
}

template<size_t nbits, size_t es>
value<2*(nbits - 2 - es)> quire_mul(const posit<nbits, es>& lhs, const posit<nbits, es>& rhs) {
	static constexpr size_t fbits = nbits - 3 - es;
	static constexpr size_t fhbits = fbits + 1;      // size of fraction + hidden bit
	static constexpr size_t mbits = 2 * fhbits;      // size of the multiplier output

	value<mbits> product;
	value<fbits> a, b;

	if (lhs.isZero() || rhs.isZero()) return product;

	// transform the inputs into (sign,scale,fraction) triples
	a.set(lhs.get_sign(), lhs.scale(), lhs.get_fraction().get(), lhs.isZero(), lhs.isNaR());;
	b.set(rhs.get_sign(), rhs.scale(), rhs.get_fraction().get(), rhs.isZero(), rhs.isNaR());;

	module_multiply(a, b, product);    // multiply the two inputs

	return product;
}

	}  // namespace unum

}  // namespace sw
