#pragma once
/*
CSV for C++, version 1.1.3
https://github.com/vincentlaucsb/csv-parser

MIT License

Copyright (c) 2017-2019 Vincent La

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef CSV_HPP
#define CSV_HPP


#endif
// Copyright 2017-2019 by Martin Moene
//
// string-view lite, a C++17-like string_view for C++98 and later.
// For more information see https://github.com/martinmoene/string-view-lite
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)


#ifndef NONSTD_SV_LITE_H_INCLUDED
#define NONSTD_SV_LITE_H_INCLUDED

#define string_view_lite_MAJOR  1
#define string_view_lite_MINOR  1
#define string_view_lite_PATCH  0

#define string_view_lite_VERSION  nssv_STRINGIFY(string_view_lite_MAJOR) "." nssv_STRINGIFY(string_view_lite_MINOR) "." nssv_STRINGIFY(string_view_lite_PATCH)

#define nssv_STRINGIFY(  x )  nssv_STRINGIFY_( x )
#define nssv_STRINGIFY_( x )  #x

// string-view lite configuration:

#define nssv_STRING_VIEW_DEFAULT  0
#define nssv_STRING_VIEW_NONSTD   1
#define nssv_STRING_VIEW_STD      2

#if !defined( nssv_CONFIG_SELECT_STRING_VIEW )
# define nssv_CONFIG_SELECT_STRING_VIEW  ( nssv_HAVE_STD_STRING_VIEW ? nssv_STRING_VIEW_STD : nssv_STRING_VIEW_NONSTD )
#endif

#if defined( nssv_CONFIG_SELECT_STD_STRING_VIEW ) || defined( nssv_CONFIG_SELECT_NONSTD_STRING_VIEW )
# error nssv_CONFIG_SELECT_STD_STRING_VIEW and nssv_CONFIG_SELECT_NONSTD_STRING_VIEW are deprecated and removed, please use nssv_CONFIG_SELECT_STRING_VIEW=nssv_STRING_VIEW_...
#endif

#ifndef  nssv_CONFIG_STD_SV_OPERATOR
# define nssv_CONFIG_STD_SV_OPERATOR  0
#endif

#ifndef  nssv_CONFIG_USR_SV_OPERATOR
# define nssv_CONFIG_USR_SV_OPERATOR  1
#endif

#ifdef   nssv_CONFIG_CONVERSION_STD_STRING
# define nssv_CONFIG_CONVERSION_STD_STRING_CLASS_METHODS   nssv_CONFIG_CONVERSION_STD_STRING
# define nssv_CONFIG_CONVERSION_STD_STRING_FREE_FUNCTIONS  nssv_CONFIG_CONVERSION_STD_STRING
#endif

#ifndef  nssv_CONFIG_CONVERSION_STD_STRING_CLASS_METHODS
# define nssv_CONFIG_CONVERSION_STD_STRING_CLASS_METHODS  1
#endif

#ifndef  nssv_CONFIG_CONVERSION_STD_STRING_FREE_FUNCTIONS
# define nssv_CONFIG_CONVERSION_STD_STRING_FREE_FUNCTIONS  1
#endif

// Control presence of exception handling (try and auto discover):

#ifndef nssv_CONFIG_NO_EXCEPTIONS
# if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#  define nssv_CONFIG_NO_EXCEPTIONS  0
# else
#  define nssv_CONFIG_NO_EXCEPTIONS  1
# endif
#endif

// C++ language version detection (C++20 is speculative):
// Note: VC14.0/1900 (VS2015) lacks too much from C++14.

#ifndef   nssv_CPLUSPLUS
# if defined(_MSVC_LANG ) && !defined(__clang__)
#  define nssv_CPLUSPLUS  (_MSC_VER == 1900 ? 201103L : _MSVC_LANG )
# else
#  define nssv_CPLUSPLUS  __cplusplus
# endif
#endif

#define nssv_CPP98_OR_GREATER  ( nssv_CPLUSPLUS >= 199711L )
#define nssv_CPP11_OR_GREATER  ( nssv_CPLUSPLUS >= 201103L )
#define nssv_CPP11_OR_GREATER_ ( nssv_CPLUSPLUS >= 201103L )
#define nssv_CPP14_OR_GREATER  ( nssv_CPLUSPLUS >= 201402L )
#define nssv_CPP17_OR_GREATER  ( nssv_CPLUSPLUS >= 201703L )
#define nssv_CPP20_OR_GREATER  ( nssv_CPLUSPLUS >= 202000L )

// use C++17 std::string_view if available and requested:

#if nssv_CPP17_OR_GREATER && defined(__has_include )
# if __has_include( <string_view> )
#  define nssv_HAVE_STD_STRING_VIEW  1
# else
#  define nssv_HAVE_STD_STRING_VIEW  0
# endif
#else
# define  nssv_HAVE_STD_STRING_VIEW  0
#endif

#define  nssv_USES_STD_STRING_VIEW  ( (nssv_CONFIG_SELECT_STRING_VIEW == nssv_STRING_VIEW_STD) || ((nssv_CONFIG_SELECT_STRING_VIEW == nssv_STRING_VIEW_DEFAULT) && nssv_HAVE_STD_STRING_VIEW) )

#define nssv_HAVE_STARTS_WITH ( nssv_CPP20_OR_GREATER || !nssv_USES_STD_STRING_VIEW )
#define nssv_HAVE_ENDS_WITH     nssv_HAVE_STARTS_WITH

//
// Use C++17 std::string_view:
//

#if nssv_USES_STD_STRING_VIEW

#include <string_view>

// Extensions for std::string:

#if nssv_CONFIG_CONVERSION_STD_STRING_FREE_FUNCTIONS

namespace nonstd {

template< class CharT, class Traits, class Allocator = std::allocator<CharT> >
std::basic_string<CharT, Traits, Allocator>
to_string( std::basic_string_view<CharT, Traits> v, Allocator const & a = Allocator() )
{
    return std::basic_string<CharT,Traits, Allocator>( v.begin(), v.end(), a );
}

template< class CharT, class Traits, class Allocator >
std::basic_string_view<CharT, Traits>
to_string_view( std::basic_string<CharT, Traits, Allocator> const & s )
{
    return std::basic_string_view<CharT, Traits>( s.data(), s.size() );
}

// Literal operators sv and _sv:

#if nssv_CONFIG_STD_SV_OPERATOR

using namespace std::literals::string_view_literals;

#endif

#if nssv_CONFIG_USR_SV_OPERATOR

inline namespace literals {
inline namespace string_view_literals {


constexpr std::string_view operator "" _sv( const char* str, size_t len ) noexcept  // (1)
{
    return std::string_view{ str, len };
}

constexpr std::u16string_view operator "" _sv( const char16_t* str, size_t len ) noexcept  // (2)
{
    return std::u16string_view{ str, len };
}

constexpr std::u32string_view operator "" _sv( const char32_t* str, size_t len ) noexcept  // (3)
{
    return std::u32string_view{ str, len };
}

constexpr std::wstring_view operator "" _sv( const wchar_t* str, size_t len ) noexcept  // (4)
{
    return std::wstring_view{ str, len };
}

}} // namespace literals::string_view_literals

#endif // nssv_CONFIG_USR_SV_OPERATOR

} // namespace nonstd

#endif // nssv_CONFIG_CONVERSION_STD_STRING_FREE_FUNCTIONS

namespace nonstd {

using std::string_view;
using std::wstring_view;
using std::u16string_view;
using std::u32string_view;
using std::basic_string_view;

// literal "sv" and "_sv", see above

using std::operator==;
using std::operator!=;
using std::operator<;
using std::operator<=;
using std::operator>;
using std::operator>=;

using std::operator<<;

} // namespace nonstd

#else // nssv_HAVE_STD_STRING_VIEW

//
// Before C++17: use string_view lite:
//

// Compiler versions:
//
// MSVC++ 6.0  _MSC_VER == 1200 (Visual Studio 6.0)
// MSVC++ 7.0  _MSC_VER == 1300 (Visual Studio .NET 2002)
// MSVC++ 7.1  _MSC_VER == 1310 (Visual Studio .NET 2003)
// MSVC++ 8.0  _MSC_VER == 1400 (Visual Studio 2005)
// MSVC++ 9.0  _MSC_VER == 1500 (Visual Studio 2008)
// MSVC++ 10.0 _MSC_VER == 1600 (Visual Studio 2010)
// MSVC++ 11.0 _MSC_VER == 1700 (Visual Studio 2012)
// MSVC++ 12.0 _MSC_VER == 1800 (Visual Studio 2013)
// MSVC++ 14.0 _MSC_VER == 1900 (Visual Studio 2015)
// MSVC++ 14.1 _MSC_VER >= 1910 (Visual Studio 2017)

#if defined(_MSC_VER ) && !defined(__clang__)
# define nssv_COMPILER_MSVC_VER      (_MSC_VER )
# define nssv_COMPILER_MSVC_VERSION  (_MSC_VER / 10 - 10 * ( 5 + (_MSC_VER < 1900 ) ) )
#else
# define nssv_COMPILER_MSVC_VER      0
# define nssv_COMPILER_MSVC_VERSION  0
#endif

#define nssv_COMPILER_VERSION( major, minor, patch )  (10 * ( 10 * major + minor) + patch)

#if defined(__clang__)
# define nssv_COMPILER_CLANG_VERSION  nssv_COMPILER_VERSION(__clang_major__, __clang_minor__, __clang_patchlevel__)
#else
# define nssv_COMPILER_CLANG_VERSION    0
#endif

#if defined(__GNUC__) && !defined(__clang__)
# define nssv_COMPILER_GNUC_VERSION  nssv_COMPILER_VERSION(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__)
#else
# define nssv_COMPILER_GNUC_VERSION    0
#endif

// half-open range [lo..hi):
#define nssv_BETWEEN( v, lo, hi ) ( (lo) <= (v) && (v) < (hi) )

// Presence of language and library features:

#ifdef _HAS_CPP0X
# define nssv_HAS_CPP0X  _HAS_CPP0X
#else
# define nssv_HAS_CPP0X  0
#endif

// Unless defined otherwise below, consider VC14 as C++11 for variant-lite:

#if nssv_COMPILER_MSVC_VER >= 1900
# undef  nssv_CPP11_OR_GREATER
# define nssv_CPP11_OR_GREATER  1
#endif

#define nssv_CPP11_90   (nssv_CPP11_OR_GREATER_ || nssv_COMPILER_MSVC_VER >= 1500)
#define nssv_CPP11_100  (nssv_CPP11_OR_GREATER_ || nssv_COMPILER_MSVC_VER >= 1600)
#define nssv_CPP11_110  (nssv_CPP11_OR_GREATER_ || nssv_COMPILER_MSVC_VER >= 1700)
#define nssv_CPP11_120  (nssv_CPP11_OR_GREATER_ || nssv_COMPILER_MSVC_VER >= 1800)
#define nssv_CPP11_140  (nssv_CPP11_OR_GREATER_ || nssv_COMPILER_MSVC_VER >= 1900)
#define nssv_CPP11_141  (nssv_CPP11_OR_GREATER_ || nssv_COMPILER_MSVC_VER >= 1910)

#define nssv_CPP14_000  (nssv_CPP14_OR_GREATER)
#define nssv_CPP17_000  (nssv_CPP17_OR_GREATER)

// Presence of C++11 language features:

#define nssv_HAVE_CONSTEXPR_11          nssv_CPP11_140
#define nssv_HAVE_EXPLICIT_CONVERSION   nssv_CPP11_140
#define nssv_HAVE_INLINE_NAMESPACE      nssv_CPP11_140
#define nssv_HAVE_NOEXCEPT              nssv_CPP11_140
#define nssv_HAVE_NULLPTR               nssv_CPP11_100
#define nssv_HAVE_REF_QUALIFIER         nssv_CPP11_140
#define nssv_HAVE_UNICODE_LITERALS      nssv_CPP11_140
#define nssv_HAVE_USER_DEFINED_LITERALS nssv_CPP11_140
#define nssv_HAVE_WCHAR16_T             nssv_CPP11_100
#define nssv_HAVE_WCHAR32_T             nssv_CPP11_100

#if ! ( ( nssv_CPP11 && nssv_COMPILER_CLANG_VERSION ) || nssv_BETWEEN( nssv_COMPILER_CLANG_VERSION, 300, 400 ) )
# define nssv_HAVE_STD_DEFINED_LITERALS  nssv_CPP11_140
#endif

// Presence of C++14 language features:

#define nssv_HAVE_CONSTEXPR_14          nssv_CPP14_000

// Presence of C++17 language features:

#define nssv_HAVE_NODISCARD             nssv_CPP17_000

// Presence of C++ library features:

#define nssv_HAVE_STD_HASH              nssv_CPP11_120

// C++ feature usage:

#if nssv_HAVE_CONSTEXPR_11
# define nssv_constexpr  constexpr
#else
# define nssv_constexpr  /*constexpr*/
#endif

#if  nssv_HAVE_CONSTEXPR_14
# define nssv_constexpr14  constexpr
#else
# define nssv_constexpr14  /*constexpr*/
#endif

#if nssv_HAVE_EXPLICIT_CONVERSION
# define nssv_explicit  explicit
#else
# define nssv_explicit  /*explicit*/
#endif

#if nssv_HAVE_INLINE_NAMESPACE
# define nssv_inline_ns  inline
#else
# define nssv_inline_ns  /*inline*/
#endif

#if nssv_HAVE_NOEXCEPT
# define nssv_noexcept  noexcept
#else
# define nssv_noexcept  /*noexcept*/
#endif

//#if nssv_HAVE_REF_QUALIFIER
//# define nssv_ref_qual  &
//# define nssv_refref_qual  &&
//#else
//# define nssv_ref_qual  /*&*/
//# define nssv_refref_qual  /*&&*/
//#endif

#if nssv_HAVE_NULLPTR
# define nssv_nullptr  nullptr
#else
# define nssv_nullptr  NULL
#endif

#if nssv_HAVE_NODISCARD
# define nssv_nodiscard  [[nodiscard]]
#else
# define nssv_nodiscard  /*[[nodiscard]]*/
#endif

// Additional includes:

#include <algorithm>
#include <cassert>
#include <iterator>
#include <limits>
#include <ostream>
#include <string>   // std::char_traits<>

#if ! nssv_CONFIG_NO_EXCEPTIONS
# include <stdexcept>
#endif

#if nssv_CPP11_OR_GREATER
# include <type_traits>
#endif

// Clang, GNUC, MSVC warning suppression macros:

#if defined(__clang__)
# pragma clang diagnostic ignored "-Wreserved-user-defined-literal"
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Wuser-defined-literals"
#elif defined(__GNUC__)
# pragma  GCC  diagnostic push
# pragma  GCC  diagnostic ignored "-Wliteral-suffix"
#endif // __clang__

#if nssv_COMPILER_MSVC_VERSION >= 140
# define nssv_SUPPRESS_MSGSL_WARNING(expr)        [[gsl::suppress(expr)]]
# define nssv_SUPPRESS_MSVC_WARNING(code, descr)  __pragma(warning(suppress: code) )
# define nssv_DISABLE_MSVC_WARNINGS(codes)        __pragma(warning(push))  __pragma(warning(disable: codes))
#else
# define nssv_SUPPRESS_MSGSL_WARNING(expr)
# define nssv_SUPPRESS_MSVC_WARNING(code, descr)
# define nssv_DISABLE_MSVC_WARNINGS(codes)
#endif

#if defined(__clang__)
# define nssv_RESTORE_WARNINGS()  _Pragma("clang diagnostic pop")
#elif defined(__GNUC__)
# define nssv_RESTORE_WARNINGS()  _Pragma("GCC diagnostic pop")
#elif nssv_COMPILER_MSVC_VERSION >= 140
# define nssv_RESTORE_WARNINGS()  __pragma(warning(pop ))
#else
# define nssv_RESTORE_WARNINGS()
#endif

// Suppress the following MSVC (GSL) warnings:
// - C4455, non-gsl   : 'operator ""sv': literal suffix identifiers that do not
//                      start with an underscore are reserved
// - C26472, gsl::t.1 : don't use a static_cast for arithmetic conversions;
//                      use brace initialization, gsl::narrow_cast or gsl::narow
// - C26481: gsl::b.1 : don't use pointer arithmetic. Use span instead

nssv_DISABLE_MSVC_WARNINGS( 4455 26481 26472 )
//nssv_DISABLE_CLANG_WARNINGS( "-Wuser-defined-literals" )
//nssv_DISABLE_GNUC_WARNINGS( -Wliteral-suffix )

namespace nonstd { namespace sv_lite {

template
<
    class CharT,
    class Traits = std::char_traits<CharT>
>
class basic_string_view;

//
// basic_string_view:
//

template
<
    class CharT,
    class Traits /* = std::char_traits<CharT> */
>
class basic_string_view
{
public:
    // Member types:

    typedef Traits traits_type;
    typedef CharT  value_type;

    typedef CharT       * pointer;
    typedef CharT const * const_pointer;
    typedef CharT       & reference;
    typedef CharT const & const_reference;

    typedef const_pointer iterator;
    typedef const_pointer const_iterator;
    typedef std::reverse_iterator< const_iterator > reverse_iterator;
    typedef	std::reverse_iterator< const_iterator > const_reverse_iterator;

    typedef std::size_t     size_type;
    typedef std::ptrdiff_t  difference_type;

    // 24.4.2.1 Construction and assignment:

    nssv_constexpr basic_string_view() nssv_noexcept
        : data_( nssv_nullptr )
        , size_( 0 )
    {}

#if nssv_CPP11_OR_GREATER
    nssv_constexpr basic_string_view( basic_string_view const & other ) nssv_noexcept = default;
#else
    nssv_constexpr basic_string_view( basic_string_view const & other ) nssv_noexcept
        : data_( other.data_)
        , size_( other.size_)
    {}
#endif

    nssv_constexpr basic_string_view( CharT const * s, size_type count )
        : data_( s )
        , size_( count )
    {}

    nssv_constexpr basic_string_view( CharT const * s)
        : data_( s )
        , size_( Traits::length(s) )
    {}

    // Assignment:

#if nssv_CPP11_OR_GREATER
    nssv_constexpr14 basic_string_view & operator=( basic_string_view const & other ) nssv_noexcept = default;
#else
    nssv_constexpr14 basic_string_view & operator=( basic_string_view const & other ) nssv_noexcept
    {
        data_ = other.data_;
        size_ = other.size_;
        return *this;
    }
#endif

    // 24.4.2.2 Iterator support:

    nssv_constexpr const_iterator begin()  const nssv_noexcept { return data_;         }
    nssv_constexpr const_iterator end()    const nssv_noexcept { return data_ + size_; }

    nssv_constexpr const_iterator cbegin() const nssv_noexcept { return begin(); }
    nssv_constexpr const_iterator cend()   const nssv_noexcept { return end();   }

    nssv_constexpr const_reverse_iterator rbegin()  const nssv_noexcept { return const_reverse_iterator( end() );   }
    nssv_constexpr const_reverse_iterator rend()    const nssv_noexcept { return const_reverse_iterator( begin() ); }

    nssv_constexpr const_reverse_iterator crbegin() const nssv_noexcept { return rbegin(); }
    nssv_constexpr const_reverse_iterator crend()   const nssv_noexcept { return rend();   }

    // 24.4.2.3 Capacity:

    nssv_constexpr size_type size()     const nssv_noexcept { return size_; }
    nssv_constexpr size_type length()   const nssv_noexcept { return size_; }
    nssv_constexpr size_type max_size() const nssv_noexcept { return (std::numeric_limits< size_type >::max)(); }

    // since C++20
    nssv_nodiscard nssv_constexpr bool empty() const nssv_noexcept
    {
        return 0 == size_;
    }

    // 24.4.2.4 Element access:

    nssv_constexpr const_reference operator[]( size_type pos ) const
    {
        return data_at( pos );
    }

    nssv_constexpr14 const_reference at( size_type pos ) const
    {
#if nssv_CONFIG_NO_EXCEPTIONS
        assert( pos < size() );
#else
        if ( pos >= size() )
        {
            throw std::out_of_range("nonst::string_view::at()");
        }
#endif
        return data_at( pos );
    }

    nssv_constexpr const_reference front() const { return data_at( 0 );          }
    nssv_constexpr const_reference back()  const { return data_at( size() - 1 ); }

    nssv_constexpr const_pointer   data()  const nssv_noexcept { return data_; }

    // 24.4.2.5 Modifiers:

    nssv_constexpr14 void remove_prefix( size_type n )
    {
        assert( n <= size() );
        data_ += n;
        size_ -= n;
    }

    nssv_constexpr14 void remove_suffix( size_type n )
    {
        assert( n <= size() );
        size_ -= n;
    }

    nssv_constexpr14 void swap( basic_string_view & other ) nssv_noexcept
    {
        using std::swap;
        swap( data_, other.data_ );
        swap( size_, other.size_ );
    }

    // 24.4.2.6 String operations:

    size_type copy( CharT * dest, size_type n, size_type pos = 0 ) const
    {
#if nssv_CONFIG_NO_EXCEPTIONS
        assert( pos <= size() );
#else
        if ( pos > size() )
        {
            throw std::out_of_range("nonst::string_view::copy()");
        }
#endif
        const size_type rlen = (std::min)( n, size() - pos );

        (void) Traits::copy( dest, data() + pos, rlen );

        return rlen;
    }

    nssv_constexpr14 basic_string_view substr( size_type pos = 0, size_type n = npos ) const
    {
#if nssv_CONFIG_NO_EXCEPTIONS
        assert( pos <= size() );
#else
        if ( pos > size() )
        {
            throw std::out_of_range("nonst::string_view::substr()");
        }
#endif
        return basic_string_view( data() + pos, (std::min)( n, size() - pos ) );
    }

    // compare(), 6x:

    nssv_constexpr14 int compare( basic_string_view other ) const nssv_noexcept // (1)
    {
        if ( const int result = Traits::compare( data(), other.data(), (std::min)( size(), other.size() ) ) )
            return result;

        return size() == other.size() ? 0 : size() < other.size() ? -1 : 1;
    }

    nssv_constexpr int compare( size_type pos1, size_type n1, basic_string_view other ) const // (2)
    {
        return substr( pos1, n1 ).compare( other );
    }

    nssv_constexpr int compare( size_type pos1, size_type n1, basic_string_view other, size_type pos2, size_type n2 ) const // (3)
    {
        return substr( pos1, n1 ).compare( other.substr( pos2, n2 ) );
    }

    nssv_constexpr int compare( CharT const * s ) const // (4)
    {
        return compare( basic_string_view( s ) );
    }

    nssv_constexpr int compare( size_type pos1, size_type n1, CharT const * s ) const // (5)
    {
        return substr( pos1, n1 ).compare( basic_string_view( s ) );
    }

    nssv_constexpr int compare( size_type pos1, size_type n1, CharT const * s, size_type n2 ) const // (6)
    {
        return substr( pos1, n1 ).compare( basic_string_view( s, n2 ) );
    }

    // 24.4.2.7 Searching:

    // starts_with(), 3x, since C++20:

    nssv_constexpr bool starts_with( basic_string_view v ) const nssv_noexcept  // (1)
    {
        return size() >= v.size() && compare( 0, v.size(), v ) == 0;
    }

    nssv_constexpr bool starts_with( CharT c ) const nssv_noexcept  // (2)
    {
        return starts_with( basic_string_view( &c, 1 ) );
    }

    nssv_constexpr bool starts_with( CharT const * s ) const  // (3)
    {
        return starts_with( basic_string_view( s ) );
    }

    // ends_with(), 3x, since C++20:

    nssv_constexpr bool ends_with( basic_string_view v ) const nssv_noexcept  // (1)
    {
        return size() >= v.size() && compare( size() - v.size(), npos, v ) == 0;
    }

    nssv_constexpr bool ends_with( CharT c ) const nssv_noexcept  // (2)
    {
        return ends_with( basic_string_view( &c, 1 ) );
    }

    nssv_constexpr bool ends_with( CharT const * s ) const  // (3)
    {
        return ends_with( basic_string_view( s ) );
    }

    // find(), 4x:

    nssv_constexpr14 size_type find( basic_string_view v, size_type pos = 0 ) const nssv_noexcept  // (1)
    {
        return assert( v.size() == 0 || v.data() != nssv_nullptr )
            , pos >= size()
            ? npos
            : to_pos( std::search( cbegin() + pos, cend(), v.cbegin(), v.cend(), Traits::eq ) );
    }

    nssv_constexpr14 size_type find( CharT c, size_type pos = 0 ) const nssv_noexcept  // (2)
    {
        return find( basic_string_view( &c, 1 ), pos );
    }

    nssv_constexpr14 size_type find( CharT const * s, size_type pos, size_type n ) const  // (3)
    {
        return find( basic_string_view( s, n ), pos );
    }

    nssv_constexpr14 size_type find( CharT const * s, size_type pos = 0 ) const  // (4)
    {
        return find( basic_string_view( s ), pos );
    }

    // rfind(), 4x:

    nssv_constexpr14 size_type rfind( basic_string_view v, size_type pos = npos ) const nssv_noexcept  // (1)
    {
        if ( size() < v.size() )
            return npos;

        if ( v.empty() )
            return (std::min)( size(), pos );

        const_iterator last   = cbegin() + (std::min)( size() - v.size(), pos ) + v.size();
        const_iterator result = std::find_end( cbegin(), last, v.cbegin(), v.cend(), Traits::eq );

        return result != last ? size_type( result - cbegin() ) : npos;
    }

    nssv_constexpr14 size_type rfind( CharT c, size_type pos = npos ) const nssv_noexcept  // (2)
    {
        return rfind( basic_string_view( &c, 1 ), pos );
    }

    nssv_constexpr14 size_type rfind( CharT const * s, size_type pos, size_type n ) const  // (3)
    {
        return rfind( basic_string_view( s, n ), pos );
    }

    nssv_constexpr14 size_type rfind( CharT const * s, size_type pos = npos ) const  // (4)
    {
        return rfind( basic_string_view( s ), pos );
    }

    // find_first_of(), 4x:

    nssv_constexpr size_type find_first_of( basic_string_view v, size_type pos = 0 ) const nssv_noexcept  // (1)
    {
        return pos >= size()
            ? npos
            : to_pos( std::find_first_of( cbegin() + pos, cend(), v.cbegin(), v.cend(), Traits::eq ) );
    }

    nssv_constexpr size_type find_first_of( CharT c, size_type pos = 0 ) const nssv_noexcept  // (2)
    {
        return find_first_of( basic_string_view( &c, 1 ), pos );
    }

    nssv_constexpr size_type find_first_of( CharT const * s, size_type pos, size_type n ) const  // (3)
    {
        return find_first_of( basic_string_view( s, n ), pos );
    }

    nssv_constexpr size_type find_first_of(  CharT const * s, size_type pos = 0 ) const  // (4)
    {
        return find_first_of( basic_string_view( s ), pos );
    }

    // find_last_of(), 4x:

    nssv_constexpr size_type find_last_of( basic_string_view v, size_type pos = npos ) const nssv_noexcept  // (1)
    {
        return empty()
            ? npos
            : pos >= size()
            ? find_last_of( v, size() - 1 )
            : to_pos( std::find_first_of( const_reverse_iterator( cbegin() + pos + 1 ), crend(), v.cbegin(), v.cend(), Traits::eq ) );
    }

    nssv_constexpr size_type find_last_of( CharT c, size_type pos = npos ) const nssv_noexcept  // (2)
    {
        return find_last_of( basic_string_view( &c, 1 ), pos );
    }

    nssv_constexpr size_type find_last_of( CharT const * s, size_type pos, size_type count ) const  // (3)
    {
        return find_last_of( basic_string_view( s, count ), pos );
    }

    nssv_constexpr size_type find_last_of( CharT const * s, size_type pos = npos ) const  // (4)
    {
        return find_last_of( basic_string_view( s ), pos );
    }

    // find_first_not_of(), 4x:

    nssv_constexpr size_type find_first_not_of( basic_string_view v, size_type pos = 0 ) const nssv_noexcept  // (1)
    {
        return pos >= size()
            ? npos
            : to_pos( std::find_if( cbegin() + pos, cend(), not_in_view( v ) ) );
    }

    nssv_constexpr size_type find_first_not_of( CharT c, size_type pos = 0 ) const nssv_noexcept  // (2)
    {
        return find_first_not_of( basic_string_view( &c, 1 ), pos );
    }

    nssv_constexpr size_type find_first_not_of( CharT const * s, size_type pos, size_type count ) const  // (3)
    {
        return find_first_not_of( basic_string_view( s, count ), pos );
    }

    nssv_constexpr size_type find_first_not_of( CharT const * s, size_type pos = 0 ) const  // (4)
    {
        return find_first_not_of( basic_string_view( s ), pos );
    }

    // find_last_not_of(), 4x:

    nssv_constexpr size_type find_last_not_of( basic_string_view v, size_type pos = npos ) const nssv_noexcept  // (1)
    {
        return empty()
            ? npos
            : pos >= size()
            ? find_last_not_of( v, size() - 1 )
            : to_pos( std::find_if( const_reverse_iterator( cbegin() + pos + 1 ), crend(), not_in_view( v ) ) );
    }

    nssv_constexpr size_type find_last_not_of( CharT c, size_type pos = npos ) const nssv_noexcept  // (2)
    {
        return find_last_not_of( basic_string_view( &c, 1 ), pos );
    }

    nssv_constexpr size_type find_last_not_of( CharT const * s, size_type pos, size_type count ) const  // (3)
    {
        return find_last_not_of( basic_string_view( s, count ), pos );
    }

    nssv_constexpr size_type find_last_not_of( CharT const * s, size_type pos = npos ) const  // (4)
    {
        return find_last_not_of( basic_string_view( s ), pos );
    }

    // Constants:

#if nssv_CPP17_OR_GREATER
    static nssv_constexpr size_type npos = size_type(-1);
#elif nssv_CPP11_OR_GREATER
    enum : size_type { npos = size_type(-1) };
#else
    enum { npos = size_type(-1) };
#endif

private:
    struct not_in_view
    {
        const basic_string_view v;

        nssv_constexpr not_in_view( basic_string_view v ) : v( v ) {}

        nssv_constexpr bool operator()( CharT c ) const
        {
            return npos == v.find_first_of( c );
        }
    };

    nssv_constexpr size_type to_pos( const_iterator it ) const
    {
        return it == cend() ? npos : size_type( it - cbegin() );
    }

    nssv_constexpr size_type to_pos( const_reverse_iterator it ) const
    {
        return it == crend() ? npos : size_type( crend() - it - 1 );
    }

    nssv_constexpr const_reference data_at( size_type pos ) const
    {
#if nssv_BETWEEN( nssv_COMPILER_GNUC_VERSION, 1, 500 )
        return data_[pos];
#else
        return assert( pos < size() ), data_[pos];
#endif
    }

private:
    const_pointer data_;
    size_type     size_;

public:
#if nssv_CONFIG_CONVERSION_STD_STRING_CLASS_METHODS

    template< class Allocator >
    basic_string_view( std::basic_string<CharT, Traits, Allocator> const & s ) nssv_noexcept
        : data_( s.data() )
        , size_( s.size() )
    {}

#if nssv_HAVE_EXPLICIT_CONVERSION

    template< class Allocator >
    explicit operator std::basic_string<CharT, Traits, Allocator>() const
    {
        return to_string( Allocator() );
    }

#endif // nssv_HAVE_EXPLICIT_CONVERSION

#if nssv_CPP11_OR_GREATER

    template< class Allocator = std::allocator<CharT> >
    std::basic_string<CharT, Traits, Allocator>
    to_string( Allocator const & a = Allocator() ) const
    {
        return std::basic_string<CharT, Traits, Allocator>( begin(), end(), a );
    }

#else

    std::basic_string<CharT, Traits>
    to_string() const
    {
        return std::basic_string<CharT, Traits>( begin(), end() );
    }

    template< class Allocator >
    std::basic_string<CharT, Traits, Allocator>
    to_string( Allocator const & a ) const
    {
        return std::basic_string<CharT, Traits, Allocator>( begin(), end(), a );
    }

#endif // nssv_CPP11_OR_GREATER

#endif // nssv_CONFIG_CONVERSION_STD_STRING_CLASS_METHODS
};

//
// Non-member functions:
//

// 24.4.3 Non-member comparison functions:
// lexicographically compare two string views (function template):

template< class CharT, class Traits >
nssv_constexpr bool operator== (
    basic_string_view <CharT, Traits> lhs,
    basic_string_view <CharT, Traits> rhs ) nssv_noexcept
{ return lhs.compare( rhs ) == 0 ; }

template< class CharT, class Traits >
nssv_constexpr bool operator!= (
    basic_string_view <CharT, Traits> lhs,
    basic_string_view <CharT, Traits> rhs ) nssv_noexcept
{ return lhs.compare( rhs ) != 0 ; }

template< class CharT, class Traits >
nssv_constexpr bool operator< (
    basic_string_view <CharT, Traits> lhs,
    basic_string_view <CharT, Traits> rhs ) nssv_noexcept
{ return lhs.compare( rhs ) < 0 ; }

template< class CharT, class Traits >
nssv_constexpr bool operator<= (
    basic_string_view <CharT, Traits> lhs,
    basic_string_view <CharT, Traits> rhs ) nssv_noexcept
{ return lhs.compare( rhs ) <= 0 ; }

template< class CharT, class Traits >
nssv_constexpr bool operator> (
    basic_string_view <CharT, Traits> lhs,
    basic_string_view <CharT, Traits> rhs ) nssv_noexcept
{ return lhs.compare( rhs ) > 0 ; }

template< class CharT, class Traits >
nssv_constexpr bool operator>= (
    basic_string_view <CharT, Traits> lhs,
    basic_string_view <CharT, Traits> rhs ) nssv_noexcept
{ return lhs.compare( rhs ) >= 0 ; }

// Let S be basic_string_view<CharT, Traits>, and sv be an instance of S.
// Implementations shall provide sufficient additional overloads marked
// constexpr and noexcept so that an object t with an implicit conversion
// to S can be compared according to Table 67.

#if nssv_CPP11_OR_GREATER && ! nssv_BETWEEN( nssv_COMPILER_MSVC_VERSION, 100, 141 )

#define nssv_BASIC_STRING_VIEW_I(T,U)  typename std::decay< basic_string_view<T,U> >::type

#if nssv_BETWEEN( nssv_COMPILER_MSVC_VERSION, 140, 150 )
# define nssv_MSVC_ORDER(x)  , int=x
#else
# define nssv_MSVC_ORDER(x)  /*, int=x*/
#endif

// ==

template< class CharT, class Traits  nssv_MSVC_ORDER(1) >
nssv_constexpr bool operator==(
         basic_string_view  <CharT, Traits> lhs,
    nssv_BASIC_STRING_VIEW_I(CharT, Traits) rhs ) nssv_noexcept
{ return lhs.compare( rhs ) == 0; }

template< class CharT, class Traits  nssv_MSVC_ORDER(2) >
nssv_constexpr bool operator==(
    nssv_BASIC_STRING_VIEW_I(CharT, Traits) lhs,
         basic_string_view  <CharT, Traits> rhs ) nssv_noexcept
{ return lhs.size() == rhs.size() && lhs.compare( rhs ) == 0; }

// !=

template< class CharT, class Traits  nssv_MSVC_ORDER(1) >
nssv_constexpr bool operator!= (
         basic_string_view  < CharT, Traits > lhs,
    nssv_BASIC_STRING_VIEW_I( CharT, Traits ) rhs ) nssv_noexcept
{ return lhs.size() != rhs.size() || lhs.compare( rhs ) != 0 ; }

template< class CharT, class Traits  nssv_MSVC_ORDER(2) >
nssv_constexpr bool operator!= (
    nssv_BASIC_STRING_VIEW_I( CharT, Traits ) lhs,
         basic_string_view  < CharT, Traits > rhs ) nssv_noexcept
{ return lhs.compare( rhs ) != 0 ; }

// <

template< class CharT, class Traits  nssv_MSVC_ORDER(1) >
nssv_constexpr bool operator< (
         basic_string_view  < CharT, Traits > lhs,
    nssv_BASIC_STRING_VIEW_I( CharT, Traits ) rhs ) nssv_noexcept
{ return lhs.compare( rhs ) < 0 ; }

template< class CharT, class Traits  nssv_MSVC_ORDER(2) >
nssv_constexpr bool operator< (
    nssv_BASIC_STRING_VIEW_I( CharT, Traits ) lhs,
         basic_string_view  < CharT, Traits > rhs ) nssv_noexcept
{ return lhs.compare( rhs ) < 0 ; }

// <=

template< class CharT, class Traits  nssv_MSVC_ORDER(1) >
nssv_constexpr bool operator<= (
         basic_string_view  < CharT, Traits > lhs,
    nssv_BASIC_STRING_VIEW_I( CharT, Traits ) rhs ) nssv_noexcept
{ return lhs.compare( rhs ) <= 0 ; }

template< class CharT, class Traits  nssv_MSVC_ORDER(2) >
nssv_constexpr bool operator<= (
    nssv_BASIC_STRING_VIEW_I( CharT, Traits ) lhs,
         basic_string_view  < CharT, Traits > rhs ) nssv_noexcept
{ return lhs.compare( rhs ) <= 0 ; }

// >

template< class CharT, class Traits  nssv_MSVC_ORDER(1) >
nssv_constexpr bool operator> (
         basic_string_view  < CharT, Traits > lhs,
    nssv_BASIC_STRING_VIEW_I( CharT, Traits ) rhs ) nssv_noexcept
{ return lhs.compare( rhs ) > 0 ; }

template< class CharT, class Traits  nssv_MSVC_ORDER(2) >
nssv_constexpr bool operator> (
    nssv_BASIC_STRING_VIEW_I( CharT, Traits ) lhs,
         basic_string_view  < CharT, Traits > rhs ) nssv_noexcept
{ return lhs.compare( rhs ) > 0 ; }

// >=

template< class CharT, class Traits  nssv_MSVC_ORDER(1) >
nssv_constexpr bool operator>= (
         basic_string_view  < CharT, Traits > lhs,
    nssv_BASIC_STRING_VIEW_I( CharT, Traits ) rhs ) nssv_noexcept
{ return lhs.compare( rhs ) >= 0 ; }

template< class CharT, class Traits  nssv_MSVC_ORDER(2) >
nssv_constexpr bool operator>= (
    nssv_BASIC_STRING_VIEW_I( CharT, Traits ) lhs,
         basic_string_view  < CharT, Traits > rhs ) nssv_noexcept
{ return lhs.compare( rhs ) >= 0 ; }

#undef nssv_MSVC_ORDER
#undef nssv_BASIC_STRING_VIEW_I

#endif // nssv_CPP11_OR_GREATER

// 24.4.4 Inserters and extractors:

namespace detail {

template< class Stream >
void write_padding( Stream & os, std::streamsize n )
{
    for ( std::streamsize i = 0; i < n; ++i )
        os.rdbuf()->sputc( os.fill() );
}

template< class Stream, class View >
Stream & write_to_stream( Stream & os, View const & sv )
{
    typename Stream::sentry sentry( os );

    if ( !os )
        return os;

    const std::streamsize length = static_cast<std::streamsize>( sv.length() );

    // Whether, and how, to pad:
    const bool      pad = ( length < os.width() );
    const bool left_pad = pad && ( os.flags() & std::ios_base::adjustfield ) == std::ios_base::right;

    if ( left_pad )
        write_padding( os, os.width() - length );

    // Write span characters:
    os.rdbuf()->sputn( sv.begin(), length );

    if ( pad && !left_pad )
        write_padding( os, os.width() - length );

    // Reset output stream width:
    os.width( 0 );

    return os;
}

} // namespace detail

template< class CharT, class Traits >
std::basic_ostream<CharT, Traits> &
operator<<(
    std::basic_ostream<CharT, Traits>& os,
    basic_string_view <CharT, Traits> sv )
{
    return detail::write_to_stream( os, sv );
}

// Several typedefs for common character types are provided:

typedef basic_string_view<char>      string_view;
typedef basic_string_view<wchar_t>   wstring_view;
#if nssv_HAVE_WCHAR16_T
typedef basic_string_view<char16_t>  u16string_view;
typedef basic_string_view<char32_t>  u32string_view;
#endif

}} // namespace nonstd::sv_lite

//
// 24.4.6 Suffix for basic_string_view literals:
//

#if nssv_HAVE_USER_DEFINED_LITERALS

namespace nonstd {
nssv_inline_ns namespace literals {
nssv_inline_ns namespace string_view_literals {

#if nssv_CONFIG_STD_SV_OPERATOR && nssv_HAVE_STD_DEFINED_LITERALS

nssv_constexpr nonstd::sv_lite::string_view operator "" sv( const char* str, size_t len ) nssv_noexcept  // (1)
{
    return nonstd::sv_lite::string_view{ str, len };
}

nssv_constexpr nonstd::sv_lite::u16string_view operator "" sv( const char16_t* str, size_t len ) nssv_noexcept  // (2)
{
    return nonstd::sv_lite::u16string_view{ str, len };
}

nssv_constexpr nonstd::sv_lite::u32string_view operator "" sv( const char32_t* str, size_t len ) nssv_noexcept  // (3)
{
    return nonstd::sv_lite::u32string_view{ str, len };
}

nssv_constexpr nonstd::sv_lite::wstring_view operator "" sv( const wchar_t* str, size_t len ) nssv_noexcept  // (4)
{
    return nonstd::sv_lite::wstring_view{ str, len };
}

#endif // nssv_CONFIG_STD_SV_OPERATOR && nssv_HAVE_STD_DEFINED_LITERALS

#if nssv_CONFIG_USR_SV_OPERATOR

nssv_constexpr nonstd::sv_lite::string_view operator "" _sv( const char* str, size_t len ) nssv_noexcept  // (1)
{
    return nonstd::sv_lite::string_view{ str, len };
}

nssv_constexpr nonstd::sv_lite::u16string_view operator "" _sv( const char16_t* str, size_t len ) nssv_noexcept  // (2)
{
    return nonstd::sv_lite::u16string_view{ str, len };
}

nssv_constexpr nonstd::sv_lite::u32string_view operator "" _sv( const char32_t* str, size_t len ) nssv_noexcept  // (3)
{
    return nonstd::sv_lite::u32string_view{ str, len };
}

nssv_constexpr nonstd::sv_lite::wstring_view operator "" _sv( const wchar_t* str, size_t len ) nssv_noexcept  // (4)
{
    return nonstd::sv_lite::wstring_view{ str, len };
}

#endif // nssv_CONFIG_USR_SV_OPERATOR

}}} // namespace nonstd::literals::string_view_literals

#endif

//
// Extensions for std::string:
//

#if nssv_CONFIG_CONVERSION_STD_STRING_FREE_FUNCTIONS

namespace nonstd {
namespace sv_lite {

// Exclude MSVC 14 (19.00): it yields ambiguous to_string():

#if nssv_CPP11_OR_GREATER && nssv_COMPILER_MSVC_VERSION != 140

template< class CharT, class Traits, class Allocator = std::allocator<CharT> >
std::basic_string<CharT, Traits, Allocator>
to_string( basic_string_view<CharT, Traits> v, Allocator const & a = Allocator() )
{
    return std::basic_string<CharT,Traits, Allocator>( v.begin(), v.end(), a );
}

#else

template< class CharT, class Traits >
std::basic_string<CharT, Traits>
to_string( basic_string_view<CharT, Traits> v )
{
    return std::basic_string<CharT, Traits>( v.begin(), v.end() );
}

template< class CharT, class Traits, class Allocator >
std::basic_string<CharT, Traits, Allocator>
to_string( basic_string_view<CharT, Traits> v, Allocator const & a )
{
    return std::basic_string<CharT, Traits, Allocator>( v.begin(), v.end(), a );
}

#endif // nssv_CPP11_OR_GREATER

template< class CharT, class Traits, class Allocator >
basic_string_view<CharT, Traits>
to_string_view( std::basic_string<CharT, Traits, Allocator> const & s )
{
    return basic_string_view<CharT, Traits>( s.data(), s.size() );
}

}} // namespace nonstd::sv_lite

#endif // nssv_CONFIG_CONVERSION_STD_STRING_FREE_FUNCTIONS

//
// make types and algorithms available in namespace nonstd:
//

namespace nonstd {

using sv_lite::basic_string_view;
using sv_lite::string_view;
using sv_lite::wstring_view;

#if nssv_HAVE_WCHAR16_T
using sv_lite::u16string_view;
#endif
#if nssv_HAVE_WCHAR32_T
using sv_lite::u32string_view;
#endif

// literal "sv"

using sv_lite::operator==;
using sv_lite::operator!=;
using sv_lite::operator<;
using sv_lite::operator<=;
using sv_lite::operator>;
using sv_lite::operator>=;

using sv_lite::operator<<;

#if nssv_CONFIG_CONVERSION_STD_STRING_FREE_FUNCTIONS
using sv_lite::to_string;
using sv_lite::to_string_view;
#endif

} // namespace nonstd

// 24.4.5 Hash support (C++11):

// Note: The hash value of a string view object is equal to the hash value of
// the corresponding string object.

#if nssv_HAVE_STD_HASH

#include <functional>

namespace std {

template<>
struct hash< nonstd::string_view >
{
public:
    std::size_t operator()( nonstd::string_view v ) const nssv_noexcept
    {
        return std::hash<std::string>()( std::string( v.data(), v.size() ) );
    }
};

template<>
struct hash< nonstd::wstring_view >
{
public:
    std::size_t operator()( nonstd::wstring_view v ) const nssv_noexcept
    {
        return std::hash<std::wstring>()( std::wstring( v.data(), v.size() ) );
    }
};

template<>
struct hash< nonstd::u16string_view >
{
public:
    std::size_t operator()( nonstd::u16string_view v ) const nssv_noexcept
    {
        return std::hash<std::u16string>()( std::u16string( v.data(), v.size() ) );
    }
};

template<>
struct hash< nonstd::u32string_view >
{
public:
    std::size_t operator()( nonstd::u32string_view v ) const nssv_noexcept
    {
        return std::hash<std::u32string>()( std::u32string( v.data(), v.size() ) );
    }
};

} // namespace std

#endif // nssv_HAVE_STD_HASH

nssv_RESTORE_WARNINGS()

#endif // nssv_HAVE_STD_STRING_VIEW
#endif // NONSTD_SV_LITE_H_INCLUDED


#define SUPPRESS_UNUSED_WARNING(x) (void)x

namespace csv {
    #if __cplusplus >= 201703L
        #include <string_view>
        using string_view = std::string_view;
    #else
        using string_view = nonstd::string_view;
    #endif
}
#include <string>
#include <vector>

namespace csv {
    /**
     *  @brief Stores information about how to parse a CSV file
     *
     *   - Can be used to initialize a csv::CSVReader() object
     *   - The preferred way to pass CSV format information between functions
     *
     *  @see csv::DEFAULT_CSV, csv::GUESS_CSV
     *
     */
    struct CSVFormat {
        char delim;
        char quote_char;

        /**< @brief Row number with columns (ignored if col_names is non-empty) */
        int header;

        /**< @brief Should be left empty unless file doesn't include header */
        std::vector<std::string> col_names;

        /**< @brief RFC 4180 non-compliance -> throw an error */
        bool strict;

        /**< @brief Detect and strip out Unicode byte order marks */
        bool unicode_detect;
    };
}
#include <iostream>
#include <vector>
#include <string>
#include <fstream>

namespace csv {
    /** @file
     *  A standalone header file for writing delimiter-separated files
     */

    /** @name CSV Writing */
    ///@{
    #ifndef DOXYGEN_SHOULD_SKIP_THIS
    template<char Delim = ',', char Quote = '"'>
    inline std::string csv_escape(const std::string& in, const bool quote_minimal = true) {
        /** Format a string to be RFC 4180-compliant
         *  @param[in]  in              String to be CSV-formatted
         *  @param[out] quote_minimal   Only quote fields if necessary.
         *                              If False, everything is quoted.
         */

        std::string new_string;
        bool quote_escape = false;     // Do we need a quote escape
        new_string += Quote;           // Start initial quote escape sequence

        for (size_t i = 0; i < in.size(); i++) {
            switch (in[i]) {
            case Quote:
                new_string += std::string(2, Quote);
                quote_escape = true;
                break;
            case Delim:
                quote_escape = true;
                // Do not break;
            default:
                new_string += in[i];
            }
        }

        if (quote_escape || !quote_minimal) {
            new_string += Quote; // Finish off quote escape
            return new_string;
        }
        else {
            return in;
        }
    }
    #endif

    /** 
     *  @brief Class for writing delimiter separated values files
     *
     *  To write formatted strings, one should
     *   -# Initialize a DelimWriter with respect to some output stream 
     *      (e.g. std::ifstream or std::stringstream)
     *   -# Call write_row() on std::vector<std::string>s of unformatted text
     *
     *  **Hint**: Use the aliases CSVWriter<OutputStream> to write CSV
     *  formatted strings and TSVWriter<OutputStream>
     *  to write tab separated strings
     */
    template<class OutputStream, char Delim, char Quote>
    class DelimWriter {
    public:
        DelimWriter(OutputStream& _out) : out(_out) {};
        DelimWriter(const std::string& filename) : DelimWriter(std::ifstream(filename)) {};

        void write_row(const std::vector<std::string>& record, bool quote_minimal = true) {
            /** Format a sequence of strings and write to CSV according to RFC 4180
            *
            *  **Note**: This does not check to make sure row lengths are consistent
            *  @param[in]  record          Vector of strings to be formatted
            *  @param      quote_minimal   Only quote fields if necessary
            */

            for (size_t i = 0, ilen = record.size(); i < ilen; i++) {
                out << csv_escape<Delim, Quote>(record[i], quote_minimal);
                if (i + 1 != ilen) out << Delim;
            }

            out << std::endl;
        }

        DelimWriter& operator<<(const std::vector<std::string>& record) {
            /** Calls write_row() on record */
            this->write_row(record);
            return *this;
        }

    private:
        OutputStream & out;
    };

    /* Uncomment when C++17 support is better
    template<class OutputStream>
    DelimWriter(OutputStream&) -> DelimWriter<OutputStream>;
    */

    /** @typedef CSVWriter
     *  @brief   Class for writing CSV files
     */
    template<class OutputStream>
    using CSVWriter = DelimWriter<OutputStream, ',', '"'>;

    /** @typedef TSVWriter
     *  @brief    Class for writing tab-separated values files
     */
    template<class OutputStream>
    using TSVWriter = DelimWriter<OutputStream, '\t', '"'>;

    //
    // Temporary: Until more C++17 compilers support template deduction guides
    //
    template<class OutputStream>
    inline CSVWriter<OutputStream> make_csv_writer(OutputStream& out) {
        /** Return a CSVWriter over the output stream */
        return CSVWriter<OutputStream>(out);
    }

    template<class OutputStream>
    inline TSVWriter<OutputStream> make_tsv_writer(OutputStream& out) {
        /** Return a TSVWriter over the output stream */
        return TSVWriter<OutputStream>(out);
    }

    ///@}
}
#include <math.h>
#include <cctype>
#include <string>


namespace csv {
    /** Enumerates the different CSV field types that are
    *  recognized by this library
    *
    *  - 0. CSV_NULL (empty string)
    *  - 1. CSV_STRING
    *  - 2. CSV_INT
    *  - 3. CSV_LONG_INT
    *  - 4. CSV_LONG_LONG_INT
    *  - 5. CSV_DOUBLE
    *
    *  **Note**: Overflowing integers will be stored and classified as doubles.
    *  Furthermore, the same number may either be a CSV_LONG_INT or CSV_INT depending on
    *  compiler and platform.
    */
    enum DataType {
        CSV_NULL,
        CSV_STRING,
        CSV_INT,
        CSV_LONG_INT,
        CSV_LONG_LONG_INT,
        CSV_DOUBLE
    };

    namespace internals {
        template<typename T>
        DataType type_num();

        template<> inline DataType type_num<int>() { return CSV_INT; }
        template<> inline DataType type_num<long int>() { return CSV_LONG_INT; }
        template<> inline DataType type_num<long long int>() { return CSV_LONG_LONG_INT; }
        template<> inline DataType type_num<double>() { return CSV_DOUBLE; }
        template<> inline DataType type_num<long double>() { return CSV_DOUBLE; }
        template<> inline DataType type_num<std::nullptr_t>() { return CSV_NULL; }
        template<> inline DataType type_num<std::string>() { return CSV_STRING; }

        /* Compute 10 to the power of n */
        template<typename T>
        const long double pow10(const T& n) {
            long double multiplicand = n > 0 ? 10 : 0.1,
                ret = 1;
            T iterations = n > 0 ? n : -n;
            
            for (T i = 0; i < iterations; i++) {
                ret *= multiplicand;
            }

            return ret;
        }

        std::string type_name(const DataType&);
        DataType data_type(csv::string_view in, long double* const out = nullptr);
    }
}
#include <memory>


namespace csv {
    namespace internals {
        /** Class for reducing number of new string malloc() calls */
        class GiantStringBuffer {
        public:
            csv::string_view get_row();
            size_t size() const;
            std::string* get() const;
            std::string* operator->() const;
            std::shared_ptr<std::string> buffer = std::make_shared<std::string>();
            void reset();

        private:
            size_t current_end = 0;
        };
    }
}
// Auxiliary data structures for CSV parser


#include <math.h>
#include <vector>
#include <string>
#include <iterator>
#include <unordered_map> // For ColNames
#include <memory> // For CSVField
#include <limits> // For CSVField

namespace csv {
    namespace internals {
        /** @struct ColNames
         *  @brief A data structure for handling column name information.
         *
         *  These are created by CSVReader and passed (via smart pointer)
         *  to CSVRow objects it creates, thus
         *  allowing for indexing by column name.
         */
        struct ColNames {
            ColNames(const std::vector<std::string>&);
            std::vector<std::string> col_names;
            std::unordered_map<std::string, size_t> col_pos;

            std::vector<std::string> get_col_names() const;
            size_t size() const;
        };
    }

    /**
    * @class CSVField
    * @brief Data type representing individual CSV values. 
    *        CSVFields can be obtained by using CSVRow::operator[]
    */
    class CSVField {
    public:
        CSVField(csv::string_view _sv) : sv(_sv) { };

        /** Returns the value casted to the requested type, performing type checking before.
        *  An std::runtime_error will be thrown if a type mismatch occurs, with the exception
        *  of T = std::string, in which the original string representation is always returned.
        *  Converting long ints to ints will be checked for overflow.
        *
        *  **Valid options for T**:
        *   - std::string or csv::string_view
        *   - int
        *   - long
        *   - long long
        *   - double
        *   - long double
        */
        template<typename T=csv::string_view> T get() {
            auto dest_type = internals::type_num<T>();
            if (dest_type >= CSV_INT && is_num()) {
                if (internals::type_num<T>() < this->type())
                    throw std::runtime_error("Overflow error.");

                return static_cast<T>(this->value);
            }

            throw std::runtime_error("Attempted to convert a value of type " + 
                internals::type_name(type()) + " to " + internals::type_name(dest_type) + ".");
        }

        bool operator==(csv::string_view other) const;
        bool operator==(const long double& other);

        DataType type();
        bool is_null() { return type() == CSV_NULL; }
        bool is_str() { return type() == CSV_STRING; }
        bool is_num() { return type() >= CSV_INT; }
        bool is_int() {
            return (type() >= CSV_INT) && (type() <= CSV_LONG_LONG_INT);
        }
        bool is_float() { return type() == CSV_DOUBLE; };

    private:
        long double value = 0;
        csv::string_view sv = "";
        int _type = -1;
        void get_value();
    };

    /**
     * @class CSVRow 
     * @brief Data structure for representing CSV rows
     *
     * Internally, a CSVRow consists of:
     *  - A pointer to the original column names
     *  - A string containing the entire CSV row (row_str)
     *  - An array of positions in that string where individual fields begin (splits)
     *
     * CSVRow::operator[] uses splits to compute a string_view over row_str.
     *
     */
    class CSVRow {
    public:
        CSVRow() = default;
        CSVRow(
            std::shared_ptr<std::string> _str,
            csv::string_view _row_str,
            std::vector<size_t>&& _splits,
            std::shared_ptr<internals::ColNames> _cnames = nullptr) :
            str(_str),
            row_str(_row_str),
            splits(std::move(_splits)),
            col_names(_cnames)
        {};

        CSVRow(
            std::string _row_str,
            std::vector<size_t>&& _splits,
            std::shared_ptr<internals::ColNames> _cnames = nullptr
            ) :
            str(std::make_shared<std::string>(_row_str)),
            splits(std::move(_splits)),
            col_names(_cnames)
        {
            row_str = csv::string_view(this->str->c_str());
        };

        bool empty() const { return this->row_str.empty(); }
        size_t size() const;

        /** @name Value Retrieval */
        ///@{
        CSVField operator[](size_t n) const;
        CSVField operator[](const std::string&) const;
        csv::string_view get_string_view(size_t n) const;
        operator std::vector<std::string>() const;
        ///@}

        /** @brief A random access iterator over the contents of a CSV row.
         *         Each iterator points to a CSVField.
         */
        class iterator {
        public:
            using value_type = CSVField;
            using difference_type = int;

            // Using CSVField * as pointer type causes segfaults in MSVC debug builds
            // but using shared_ptr as pointer type won't compile in g++
            #ifdef _MSC_BUILD
            using pointer = std::shared_ptr<CSVField> ;
            #else
            using pointer = CSVField * ;
            #endif

            using reference = CSVField & ;
            using iterator_category = std::random_access_iterator_tag;

            iterator(const CSVRow*, int i);

            reference operator*() const;
            pointer operator->() const;

            iterator operator++(int);
            iterator& operator++();
            iterator operator--(int);
            iterator& operator--();
            iterator operator+(difference_type n) const;
            iterator operator-(difference_type n) const;

            bool operator==(const iterator&) const;
            bool operator!=(const iterator& other) const { return !operator==(other); }

            #ifndef NDEBUG
            friend CSVRow;
            #endif

        private:
            const CSVRow * daddy = nullptr;            // Pointer to parent
            std::shared_ptr<CSVField> field = nullptr; // Current field pointed at
            int i = 0;                                 // Index of current field
        };

        /** @brief A reverse iterator over the contents of a CSVRow. */
        using reverse_iterator = std::reverse_iterator<iterator>;

        /** @name Iterators
         *  @brief Each iterator points to a CSVField object.
         */
        ///@{
        iterator begin() const;
        iterator end() const;
        reverse_iterator rbegin() const;
        reverse_iterator rend() const;
        ///@}

    private:
		std::shared_ptr<std::string> str = nullptr;
		csv::string_view row_str = "";
		std::vector<size_t> splits = {};
        std::shared_ptr<internals::ColNames> col_names = nullptr;
    };

    // get() specializations
    template<>
    inline std::string CSVField::get<std::string>() {
        return std::string(this->sv);
    }

    template<>
    inline csv::string_view CSVField::get<csv::string_view>() {
        return this->sv;
    }

    template<>
    inline long double CSVField::get<long double>() {
        if (!is_num())
            throw std::runtime_error("Not a number.");

        return this->value;
    }
}
#include <deque>


namespace csv {
    namespace internals {
        // Get operating system specific details
        #if defined(_WIN32)
            #include <Windows.h>
            #undef max
            #undef min

            inline int getpagesize() {
                _SYSTEM_INFO sys_info = {};
                GetSystemInfo(&sys_info);
                return sys_info.dwPageSize;
            }

            const int PAGE_SIZE = getpagesize();
        #elif defined(__linux__) 
            #include <unistd.h>
            const int PAGE_SIZE = getpagesize();
        #else
            const int PAGE_SIZE = 4096;
        #endif

        /** @brief For functions that lazy load a large CSV, this determines how
         *         many bytes are read at a time
         */
        const size_t ITERATION_CHUNK_SIZE = 10000000; // 10MB
    }

    /** @brief Used for counting number of rows */
    using RowCount = long long int;

    using CSVCollection = std::deque<CSVRow>;

    /** @name Global Constants */
    ///@{
    /** @brief A dummy variable used to indicate delimiter should be guessed */
    const CSVFormat GUESS_CSV = { '\0', '"', 0, {}, false, true };

    /** @brief RFC 4180 CSV format */
    const CSVFormat DEFAULT_CSV = { ',', '"', 0, {}, false, true };

    /** @brief RFC 4180 CSV format with strict parsing */
    const CSVFormat DEFAULT_CSV_STRICT = { ',', '"', 0, {}, true, true };
    ///@}
}
#include <deque>
#include <iterator>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>


/** @namespace csv
 *  @brief The all encompassing namespace
 */
namespace csv {
    /** @brief Integer indicating a requested column wasn't found. */
    const int CSV_NOT_FOUND = -1;

    /** @namespace csv::internals
     *  @brief Stuff that is generally not of interest to end-users
     */
    namespace internals {
        std::string type_name(const DataType& dtype);
        std::string format_row(const std::vector<std::string>& row, const std::string& delim = ", ");
    }

    /** @class CSVReader
     *  @brief Main class for parsing CSVs from files and in-memory sources
     *
     *  All rows are compared to the column names for length consistency
     *  - By default, rows that are too short or too long are dropped
     *  - Custom behavior can be defined by overriding bad_row_handler in a subclass
     */
    class CSVReader {
    public:
        /**
         * @brief An input iterator capable of handling large files.
         * Created by CSVReader::begin() and CSVReader::end().
         *
         * **Iterating over a file:**
         * \snippet tests/test_csv_iterator.cpp CSVReader Iterator 1
         *
         * **Using with <algorithm> library:**
         * \snippet tests/test_csv_iterator.cpp CSVReader Iterator 2
         */
        class iterator {
        public:
            using value_type = CSVRow;
            using difference_type = std::ptrdiff_t;
            using pointer = CSVRow * ;
            using reference = CSVRow & ;
            using iterator_category = std::input_iterator_tag;

            iterator() = default;
            iterator(CSVReader* reader) : daddy(reader) {};
            iterator(CSVReader*, CSVRow&&);

            reference operator*();
            pointer operator->();
            iterator& operator++(); // Pre-inc
            iterator operator++(int); // Post-inc
            iterator& operator--();

            bool operator==(const iterator&) const;
            bool operator!=(const iterator& other) const { return !operator==(other); }

        private:
            CSVReader * daddy = nullptr;  // Pointer to parent
            CSVRow row;                   // Current row
            RowCount i = 0;               // Index of current row
        };

        /** @name Constructors
         *  Constructors for iterating over large files and parsing in-memory sources.
         */
         ///@{
        CSVReader(const std::string& filename, CSVFormat format = GUESS_CSV);
        CSVReader(CSVFormat format = DEFAULT_CSV);
        ///@}

        CSVReader(const CSVReader&) = delete; // No copy constructor
        CSVReader(CSVReader&&) = default;     // Move constructor
        CSVReader& operator=(const CSVReader&) = delete; // No copy assignment
        CSVReader& operator=(CSVReader&& other) = default;
        ~CSVReader() { this->close(); }

        /** @name Reading In-Memory Strings
         *  You can piece together incomplete CSV fragments by calling feed() on them
         *  before finally calling end_feed().
         *
         *  Alternatively, you can also use the parse() shorthand function for
         *  smaller strings.
         */
         ///@{
        void feed(csv::string_view in);
        void end_feed();
        ///@}

        /** @name Retrieving CSV Rows */
        ///@{
        bool read_row(CSVRow &row);
        iterator begin();
        iterator end();
        ///@}

        /** @name CSV Metadata */
        ///@{
        CSVFormat get_format() const;
        std::vector<std::string> get_col_names() const;
        int index_of(const std::string& col_name) const;
        ///@}
        
        /** @name CSV Metadata: Attributes */
        ///@{
        RowCount row_num = 0;        /**< @brief How many lines have
                                      *    been parsed so far
                                      */
        RowCount correct_rows = 0;   /**< @brief How many correct rows
                                      *    (minus header) have been parsed so far
                                      */
        bool utf8_bom = false;       /**< @brief Set to true if UTF-8 BOM was detected */
        ///@}

        void close();               /**< @brief Close the open file handle.
                                    *   Automatically called by ~CSVReader().
                                    */

        friend CSVCollection parse(const std::string&, CSVFormat);
    protected:
        /**
         * \defgroup csv_internal CSV Parser Internals
         * @brief Internals of CSVReader. Only maintainers and those looking to
         *        extend the parser should read this.
         * @{
         */

         /**  @typedef ParseFlags
          *   @brief   An enum used for describing the significance of each character
          *            with respect to CSV parsing
          */
        enum ParseFlags {
            NOT_SPECIAL,
            QUOTE,
            DELIMITER,
            NEWLINE
        };

        using WorkItem = std::pair<std::unique_ptr<char[]>, size_t>; /**<
            @brief A string buffer and its size */

        std::vector<CSVReader::ParseFlags> make_flags() const;

        internals::GiantStringBuffer record_buffer; /**<
            @brief Buffer for current row being parsed */

        std::vector<size_t> split_buffer; /**<
            @brief Positions where current row is split */

        std::deque<CSVRow> records; /**< @brief Queue of parsed CSV rows */
        inline bool eof() { return !(this->infile); };

        /** @name CSV Parsing Callbacks
         *  The heart of the CSV parser.
         *  These methods are called by feed().
        */
        ///@{
        void write_record();
        virtual void bad_row_handler(std::vector<std::string>);
        ///@}

        /** @name CSV Settings **/
        ///@{
        char delimiter;                /**< @brief Delimiter character */
        char quote_char;               /**< @brief Quote character */
        int header_row;                /**< @brief Line number of the header row (zero-indexed) */
        bool strict = false;           /**< @brief Strictness of parser */

        std::vector<CSVReader::ParseFlags> parse_flags; /**< @brief
        A table where the (i + 128)th slot gives the ParseFlags for ASCII character i */
        ///@}

        /** @name Parser State */
        ///@{
        /** <@brief Pointer to a object containing column information
        */
        std::shared_ptr<internals::ColNames> col_names =
            std::make_shared<internals::ColNames>(std::vector<std::string>({}));

        /** <@brief Whether or not an attempt to find Unicode BOM has been made */
        bool unicode_bom_scan = false;
        ///@}

        /** @name Multi-Threaded File Reading Functions */
        ///@{
        void feed(WorkItem&&); /**< @brief Helper for read_csv_worker() */
        void read_csv(
            const std::string& filename,
            const size_t& bytes = internals::ITERATION_CHUNK_SIZE
        );
        void read_csv_worker();
        ///@}

        /** @name Multi-Threaded File Reading: Flags and State */
        ///@{
        std::FILE* infile = nullptr;         /**< @brief Current file handle.
                                                  Destroyed by ~CSVReader(). */

        std::deque<WorkItem> feed_buffer;                     /**< @brief Message queue for worker */

        std::mutex feed_lock;                /**< @brief Allow only one worker to write */
        std::condition_variable feed_cond;   /**< @brief Wake up worker */
        ///@} 

        /**@}*/ // End of parser internals
    };

    namespace internals {
        /** Class for guessing the delimiter & header row number of CSV files */
        class CSVGuesser {
            struct Guesser : public CSVReader {
                using CSVReader::CSVReader;
                void bad_row_handler(std::vector<std::string> record) override;
                friend CSVGuesser;

                // Frequency counter of row length
                std::unordered_map<size_t, size_t> row_tally = { { 0, 0 } };

                // Map row lengths to row num where they first occurred
                std::unordered_map<size_t, size_t> row_when = { { 0, 0 } };
            };

        public:
            CSVGuesser(const std::string& _filename) : filename(_filename) {};
            std::vector<char> delims = { ',', '|', '\t', ';', '^' };
            void guess_delim();
            bool first_guess();
            void second_guess();

            char delim;
            int header_row = 0;

        private:
            void get_csv_head();
            std::string filename;
            std::string head;
        };
    }
}
#include <unordered_map>
#include <vector>

namespace csv {
    /** @class CSVStat
     *  @brief Class for calculating statistics from CSV files and in-memory sources
     *
     *  **Example**
     *  \include programs/csv_stats.cpp
     *
     */
    class CSVStat : public CSVReader {
    public:
        using FreqCount = std::unordered_map<std::string, RowCount>;
        using TypeCount = std::unordered_map<DataType, RowCount>;

        void end_feed();
        std::vector<long double> get_mean() const;
        std::vector<long double> get_variance() const;
        std::vector<long double> get_mins() const;
        std::vector<long double> get_maxes() const;
        std::vector<FreqCount> get_counts() const;
        std::vector<TypeCount> get_dtypes() const;

        CSVStat(std::string filename, CSVFormat format = GUESS_CSV);
        CSVStat(CSVFormat format = DEFAULT_CSV) : CSVReader(format) {};
    private:
        // An array of rolling averages
        // Each index corresponds to the rolling mean for the column at said index
        std::vector<long double> rolling_means;
        std::vector<long double> rolling_vars;
        std::vector<long double> mins;
        std::vector<long double> maxes;
        std::vector<FreqCount> counts;
        std::vector<TypeCount> dtypes;
        std::vector<long double> n;

        // Statistic calculators
        void variance(const long double&, const size_t&);
        void count(CSVField&, const size_t&);
        void min_max(const long double&, const size_t&);
        void dtype(CSVField&, const size_t&);

        void calc();
        void calc_worker(const size_t&);
    };
}

#include <string>

namespace csv {
    /** Returned by get_file_info() */
    struct CSVFileInfo {
        std::string filename;               /**< Filename */
        std::vector<std::string> col_names; /**< CSV column names */
        char delim;                         /**< Delimiting character */
        RowCount n_rows;                    /**< Number of rows in a file */
        int n_cols;                         /**< Number of columns in a CSV */
    };

    /** @name Shorthand Parsing Functions
     *  @brief Convienience functions for parsing small strings
     */
     ///@{
    CSVCollection operator ""_csv(const char*, size_t);
    CSVCollection parse(const std::string& in, CSVFormat format = DEFAULT_CSV);
    ///@}

    /** @name Utility Functions */
    ///@{
    std::unordered_map<std::string, DataType> csv_data_types(const std::string&);
    CSVFileInfo get_file_info(const std::string& filename);
    CSVFormat guess_format(const std::string& filename);
    std::vector<std::string> get_col_names(
        const std::string& filename,
        const CSVFormat format = GUESS_CSV);
    int get_col_pos(const std::string filename, const std::string col_name,
        const CSVFormat format = GUESS_CSV);
    ///@}

    namespace internals {
        template<typename T>
        inline bool is_equal(T a, T b, T epsilon = 0.001) {
            /** Returns true if two doubles are about the same */
            return std::abs(a - b) < epsilon;
        }
    }
}

#include <algorithm>
#include <cstdio>   // For read_csv()
#include <cstring>  // For read_csv()
#include <fstream>
#include <sstream>


/** @file
 *  @brief Defines all functionality needed for basic CSV parsing
 */

namespace csv {
    namespace internals {
        std::string format_row(const std::vector<std::string>& row, const std::string& delim) {
            /** Print a CSV row */
            std::stringstream ret;
            for (size_t i = 0; i < row.size(); i++) {
                ret << row[i];
                if (i + 1 < row.size()) ret << delim;
                else ret << std::endl;
            }

            return ret.str();
        }

        //
        // CSVGuesser
        //
        void CSVGuesser::Guesser::bad_row_handler(std::vector<std::string> record) {
            /** Helps CSVGuesser tally up the size of rows encountered while parsing */
            if (row_tally.find(record.size()) != row_tally.end()) row_tally[record.size()]++;
            else {
                row_tally[record.size()] = 1;
                row_when[record.size()] = this->row_num + 1;
            }
        }

        void CSVGuesser::guess_delim() {
            /** Guess the delimiter of a CSV by scanning the first 100 lines by
            *  First assuming that the header is on the first row
            *  If the first guess returns too few rows, then we move to the second
            *  guess method
            */
            if (!first_guess()) second_guess();
        }

        bool CSVGuesser::first_guess() {
            /** Guess the delimiter of a delimiter separated values file
             *  by scanning the first 100 lines
             *
             *  - "Winner" is based on which delimiter has the most number
             *    of correctly parsed rows + largest number of columns
             *  -  **Note:** Assumes that whatever the dialect, all records
             *     are newline separated
             *
             *  Returns True if guess was a good one and second guess isn't needed
             */

            CSVFormat format = DEFAULT_CSV;
            char current_delim{ ',' };
            RowCount max_rows = 0,
                temp_rows = 0;
            size_t max_cols = 0;

            // Read first 500KB of the CSV file
            this->get_csv_head();

            for (char delim: this->delims) {
                format.delim = delim;
                CSVReader guesser(format);
                guesser.feed(this->head);
                guesser.end_feed();

                // WORKAROUND on Unix systems because certain newlines
                // get double counted
                // temp_rows = guesser.correct_rows;
                temp_rows = std::min(guesser.correct_rows, (RowCount)100);
                if ((guesser.row_num >= max_rows) &&
                    (guesser.get_col_names().size() > max_cols)) {
                    max_rows = temp_rows;
                    max_cols = guesser.get_col_names().size();
                    current_delim = delim;
                }
            }

            this->delim = current_delim;

            // If there are only a few rows/columns, trying guessing again
            return (max_rows > 10 && max_cols > 2);
        }

        void CSVGuesser::second_guess() {
            /** For each delimiter, find out which row length was most common.
             *  The delimiter with the longest mode row length wins.
             *  Then, the line number of the header row is the first row with
             *  the mode row length.
             */

            CSVFormat format = DEFAULT_CSV;
            size_t max_rlen = 0,
                header = 0;

            for (char delim: this->delims) {
                format.delim = delim;
                Guesser guess(format);
                guess.feed(this->head);
                guess.end_feed();

                // Most common row length
                auto max = std::max_element(guess.row_tally.begin(), guess.row_tally.end(),
                    [](const std::pair<size_t, size_t>& x,
                        const std::pair<size_t, size_t>& y) {
                    return x.second < y.second; });

                // Idea: If CSV has leading comments, actual rows don't start
                // until later and all actual rows get rejected because the CSV
                // parser mistakenly uses the .size() of the comment rows to
                // judge whether or not they are valid.
                // 
                // The first part of the if clause means we only change the header
                // row if (number of rejected rows) > (number of actual rows)
                if (max->second > guess.records.size() &&
                    (max->first > max_rlen)) {
                    max_rlen = max->first;
                    header = guess.row_when[max_rlen];
                }
            }

            this->header_row = static_cast<int>(header);
        }

        /** Read the first 500KB of a CSV file */
        void CSVGuesser::get_csv_head() {
            const size_t bytes = 500000;
            std::ifstream infile(this->filename);
            if (!infile.is_open()) {
                throw std::runtime_error("Cannot open file " + this->filename);
            }

            std::unique_ptr<char[]> buffer(new char[bytes + 1]);
            char * head_buffer = buffer.get();

            for (size_t i = 0; i < bytes + 1; i++) {
                head_buffer[i] = '\0';
            }

            infile.read(head_buffer, bytes);
            this->head = head_buffer;
        }
    }

    /** @brief Guess the delimiter used by a delimiter-separated values file */
    CSVFormat guess_format(const std::string& filename) {
        internals::CSVGuesser guesser(filename);
        guesser.guess_delim();
        return { guesser.delim, '"', guesser.header_row };
    }

    std::vector<CSVReader::ParseFlags> CSVReader::make_flags() const {
        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character and, v[i + 128] labels it according to
         *  the CSVReader::ParseFlags enum
         */

        std::vector<ParseFlags> ret;
        for (int i = -128; i < 128; i++) {
            char ch = char(i);

            if (ch == this->delimiter)
                ret.push_back(DELIMITER);
            else if (ch == this->quote_char)
                ret.push_back(QUOTE);
            else if (ch == '\r' || ch == '\n')
                ret.push_back(NEWLINE);
            else
                ret.push_back(NOT_SPECIAL);
        }

        return ret;
    }

    void CSVReader::bad_row_handler(std::vector<std::string> record) {
        /** Handler for rejected rows (too short or too long). This does nothing
         *  unless strict parsing was set, in which case it throws an eror.
         *  Subclasses of CSVReader may easily override this to provide
         *  custom behavior.
         */
        if (this->strict) {
            std::string problem;
            if (record.size() > this->col_names->size()) problem = "too long";
            else problem = "too short";

            throw std::runtime_error("Line " + problem + " around line " +
                std::to_string(correct_rows) + " near\n" +
                internals::format_row(record)
            );
        }
    };

    /**
     *  @brief Allows parsing in-memory sources (by calling feed() and end_feed()).
     */
    CSVReader::CSVReader(CSVFormat format) :
        delimiter(format.delim), quote_char(format.quote_char),
        header_row(format.header), strict(format.strict),
        unicode_bom_scan(!format.unicode_detect) {
        if (!format.col_names.empty()) {
            this->header_row = -1;
            this->col_names = std::make_shared<internals::ColNames>(format.col_names);
        }
    };

    /**
     *  @brief Allows reading a CSV file in chunks, using overlapped
     *         threads for simulatenously reading from disk and parsing.
     *         Rows should be retrieved with read_row() or by using
     *         CSVReader::iterator.
     *
     * **Details:** Reads the first 500kB of a CSV file to infer file information
     *              such as column names and delimiting character.
     *
     *  @param[in] filename  Path to CSV file
     *  @param[in] format    Format of the CSV file
     *
     *  \snippet tests/test_read_csv.cpp CSVField Example
     *
     */
    CSVReader::CSVReader(const std::string& filename, CSVFormat format) {
        if (format.delim == '\0')
            format = guess_format(filename);

        this->col_names = std::make_shared<internals::ColNames>(format.col_names);
        delimiter = format.delim;
        quote_char = format.quote_char;
        header_row = format.header;
        strict = format.strict;

        // Read first 500KB of CSV
        read_csv(filename, 500000);
    }

    /** @brief Return the format of the original raw CSV */
    CSVFormat CSVReader::get_format() const {
        return {
            this->delimiter,
            this->quote_char,
            this->header_row,
            this->col_names->col_names
        };
    }

    /** @brief Return the CSV's column names as a vector of strings. */
    std::vector<std::string> CSVReader::get_col_names() const {
        return this->col_names->get_col_names();
    }

    /** @brief Return the index of the column name if found or
     *         csv::CSV_NOT_FOUND otherwise.
     */
    int CSVReader::index_of(const std::string& col_name) const {
        auto _col_names = this->get_col_names();
        for (size_t i = 0; i < _col_names.size(); i++)
            if (_col_names[i] == col_name) return (int)i;

        return CSV_NOT_FOUND;
    }

    void CSVReader::feed(WorkItem&& buff) {
        this->feed( csv::string_view(buff.first.get(), buff.second) );
    }

    void CSVReader::feed(csv::string_view in) {
        /** @brief Parse a CSV-formatted string.
         *
         *  Incomplete CSV fragments can be joined together by calling feed() on them sequentially.
         *  **Note**: end_feed() should be called after the last string
         */

        if (parse_flags.empty()) parse_flags = this->make_flags();

        bool quote_escape = false;  // Are we currently in a quote escaped field?

        // Unicode BOM Handling
        if (!this->unicode_bom_scan) {
            if (in[0] == 0xEF && in[1] == 0xBB && in[2] == 0xEF) {
                in.remove_prefix(3); // Remove BOM from input string
                this->utf8_bom = true;
            }

            this->unicode_bom_scan = true;
        }

        // Optimization
        this->record_buffer->reserve(in.size());
        std::string& _record_buffer = *(this->record_buffer.get());

        const size_t in_size = in.size();
        for (size_t i = 0; i < in_size; i++) {
            switch (this->parse_flags[in[i] + 128]) {
                case DELIMITER:
                    if (!quote_escape) {
                        this->split_buffer.push_back(this->record_buffer.size());
                        break;
                    }
                case NEWLINE:
                    if (!quote_escape) {
                        // End of record -> Write record
                        if (i + 1 < in_size && in[i + 1] == '\n') // Catches CRLF (or LFLF)
                            ++i;
                        this->write_record();
                        break;
                    }
                case NOT_SPECIAL: {
                    // Optimization: Since NOT_SPECIAL characters tend to occur in contiguous
                    // sequences, use the loop below to avoid having to go through the outer
                    // switch statement as much as possible
                    #if __cplusplus >= 201703L
                    size_t start = i;
                    while (i + 1 < in_size && this->parse_flags[in[i + 1] + 128] == NOT_SPECIAL) {
                        i++;
                    }

                    _record_buffer += in.substr(start, i - start + 1);
                    #else
                    _record_buffer += in[i];

                    while (i + 1 < in_size && this->parse_flags[in[i + 1] + 128] == NOT_SPECIAL) {
                        _record_buffer += in[++i];
                    }
                    #endif

                    break;
                }
                default: // Quote
                    if (!quote_escape) {
                        // Don't deref past beginning
                        if (i && this->parse_flags[in[i - 1] + 128] >= DELIMITER) {
                            // Case: Previous character was delimiter or newline
                            quote_escape = true;
                        }

                        break;
                    }

                    auto next_ch = this->parse_flags[in[i + 1] + 128];
                    if (next_ch >= DELIMITER) {
                        // Case: Delim or newline => end of field
                        quote_escape = false;
                        break;
                    }
                        
                    // Case: Escaped quote
                    _record_buffer += in[i];

                    if (next_ch == QUOTE)
                        ++i;  // Case: Two consecutive quotes
                    else if (this->strict)
                        throw std::runtime_error("Unescaped single quote around line " +
                            std::to_string(this->correct_rows) + " near:\n" +
                            std::string(in.substr(i, 100)));
                        
                    break;
            }
        }

        this->record_buffer.reset();
    }

    void CSVReader::end_feed() {
        /** Indicate that there is no more data to receive,
         *  and handle the last row
         */
        this->write_record();
    }

    void CSVReader::write_record() {
        /** Push the current row into a queue if it is the right length.
         *  Drop it otherwise.
         */

        size_t col_names_size = this->col_names->size();

        auto row = CSVRow(
            this->record_buffer.buffer,
            this->record_buffer.get_row(),
            std::move(this->split_buffer),
            this->col_names
        );

        if (this->row_num > this->header_row) {
            // Make sure record is of the right length
            if (row.size() == col_names_size) {
                this->correct_rows++;
                this->records.push_back(std::move(row));
            }
            else {
                /* 1) Zero-length record, probably caused by extraneous newlines
                 * 2) Too short or too long
                 */
                this->row_num--;
                if (!row.empty())
                    bad_row_handler(std::vector<std::string>(row));
            }
        }
        else if (this->row_num == this->header_row) {
            this->col_names = std::make_shared<internals::ColNames>(
                std::vector<std::string>(row));
        } // else: Ignore rows before header row

        // Some memory allocation optimizations
        this->split_buffer = {};
        if (this->split_buffer.capacity() < col_names_size)
            split_buffer.reserve(col_names_size);

        this->row_num++;
    }

    void CSVReader::read_csv_worker() {
        /** @brief Worker thread for read_csv() which parses CSV rows (while the main
         *         thread pulls data from disk)
         */
        while (true) {
            std::unique_lock<std::mutex> lock{ this->feed_lock }; // Get lock
            this->feed_cond.wait(lock,                            // Wait
                [this] { return !(this->feed_buffer.empty()); });

            // Wake-up
            auto in = std::move(this->feed_buffer.front());
            this->feed_buffer.pop_front();

            // Nullptr --> Die
            if (!in.first) break;

            lock.unlock();      // Release lock
            this->feed(std::move(in));
        }
    }

    /**
     * @brief Parse a CSV file using multiple threads
     *
     * @param[in] nrows Number of rows to read. Set to -1 to read entire file.
     *
     * @see CSVReader::read_row()
     * 
     */
    void CSVReader::read_csv(const std::string& filename, const size_t& bytes) {
        if (!this->infile) {
            #ifdef _MSC_BUILD
            // Silence compiler warnings in Microsoft Visual C++
            size_t err = fopen_s(&(this->infile), filename.c_str(), "rb");
            if (err)
                throw std::runtime_error("Cannot open file " + filename);
            #else
            this->infile = std::fopen(filename.c_str(), "rb");
            if (!this->infile)
                throw std::runtime_error("Cannot open file " + filename);
            #endif
        }

        const size_t BUFFER_UPPER_LIMIT = std::min(bytes, (size_t)1000000);
        std::unique_ptr<char[]> buffer(new char[BUFFER_UPPER_LIMIT]);
        auto line_buffer = buffer.get();
        line_buffer[0] = '\0';

        std::thread worker(&CSVReader::read_csv_worker, this);

        for (size_t processed = 0; processed < bytes; ) {
            char * result = std::fgets(line_buffer, internals::PAGE_SIZE, this->infile);
            if (result == NULL) break;
            line_buffer += std::strlen(line_buffer);
            size_t current_strlen = line_buffer - buffer.get();

            if (current_strlen >= 0.9 * BUFFER_UPPER_LIMIT) {
                processed += (line_buffer - buffer.get());
                std::unique_lock<std::mutex> lock{ this->feed_lock };
                this->feed_buffer.push_back(std::make_pair<>(std::move(buffer), current_strlen));
                this->feed_cond.notify_one();

                buffer = std::unique_ptr<char[]>(new char[BUFFER_UPPER_LIMIT]); // New pointer
                line_buffer = buffer.get();
                line_buffer[0] = '\0';
            }
        }

        // Feed remaining bits
        std::unique_lock<std::mutex> lock{ this->feed_lock };
        this->feed_buffer.push_back(std::make_pair<>(std::move(buffer), line_buffer - buffer.get()));
        this->feed_buffer.push_back(std::make_pair<>(nullptr, 0)); // Termination signal
        this->feed_cond.notify_one();
        lock.unlock();
        worker.join();

        if (std::feof(this->infile)) {
            this->end_feed();
            this->close();
        }
    }

    void CSVReader::close() {
        if (this->infile) {
            std::fclose(this->infile);
            this->infile = nullptr;
        }
    }

    /**
     * @brief Retrieve rows as CSVRow objects, returning true if more rows are available.
     *
     * **Performance Notes**:
     *  - The number of rows read in at a time is determined by csv::ITERATION_CHUNK_SIZE
     *  - For performance details, read the documentation for CSVRow and CSVField.
     *
     * @param[out] row The variable where the parsed row will be stored
     * @see CSVRow, CSVField
     *
     * **Example:**
     * \snippet tests/test_read_csv.cpp CSVField Example
     *
     */
    bool CSVReader::read_row(CSVRow &row) {
        if (this->records.empty()) {
            if (!this->eof()) {
                this->read_csv("", internals::ITERATION_CHUNK_SIZE);
            }
            else return false; // Stop reading
        }

        row = std::move(this->records.front());
        this->records.pop_front();

        return true;
    }
}

namespace csv {
    /**
     * @brief Return an iterator to the first row in the reader
     *
     */
    CSVReader::iterator CSVReader::begin() {
        CSVReader::iterator ret(this, std::move(this->records.front()));
        this->records.pop_front();
        return ret;
    }

    /**
     * @brief A placeholder for the imaginary past the end row in a CSV.
     *        Attempting to deference this will lead to bad things.
     */
    CSVReader::iterator CSVReader::end() {
        return CSVReader::iterator();
    }

    /////////////////////////
    // CSVReader::iterator //
    /////////////////////////

    CSVReader::iterator::iterator(CSVReader* _daddy, CSVRow&& _row) :
        daddy(_daddy) {
        row = std::move(_row);
    }

    /** @brief Access the CSVRow held by the iterator */
    CSVReader::iterator::reference CSVReader::iterator::operator*() {
        return this->row;
    }

    /** @brief Return a pointer to the CSVRow the iterator has stopped at */
    CSVReader::iterator::pointer CSVReader::iterator::operator->() {
        return &(this->row);
    }

    /** @brief Advance the iterator by one row. If this CSVReader has an
     *  associated file, then the iterator will lazily pull more data from
     *  that file until EOF.
     */
    CSVReader::iterator& CSVReader::iterator::operator++() {
        if (!daddy->read_row(this->row)) {
            this->daddy = nullptr; // this == end()
        }

        return *this;
    }

    /** @brief Post-increment iterator */
    CSVReader::iterator CSVReader::iterator::operator++(int) {
        auto temp = *this;
        if (!daddy->read_row(this->row)) {
            this->daddy = nullptr; // this == end()
        }

        return temp;
    }

    /** @brief Returns true if iterators were constructed from the same CSVReader
     *         and point to the same row
     */
    bool CSVReader::iterator::operator==(const CSVReader::iterator& other) const {
        return (this->daddy == other.daddy) && (this->i == other.i);
    }
}
#include <cassert>
#include <functional>

namespace csv {
    namespace internals {
        //////////////
        // ColNames //
        //////////////

        ColNames::ColNames(const std::vector<std::string>& _cnames)
            : col_names(_cnames) {
            for (size_t i = 0; i < _cnames.size(); i++) {
                this->col_pos[_cnames[i]] = i;
            }
        }

        std::vector<std::string> ColNames::get_col_names() const {
            return this->col_names;
        }

        size_t ColNames::size() const {
            return this->col_names.size();
        }
    }

    /** @brief Return the number of fields in this row */
    size_t CSVRow::size() const {
        return splits.size() + 1;
    }

    /** @brief      Return a string view of the nth field
     *  @complexity Constant
     */
    csv::string_view CSVRow::get_string_view(size_t n) const {
        csv::string_view ret(this->row_str);
        size_t beg = 0,
            end = 0,
            r_size = this->size();

        if (n >= r_size)
            throw std::runtime_error("Index out of bounds.");

        if (!splits.empty()) {
            if (n == 0) {
                end = this->splits[0];
            }
            else if (r_size == 2) {
                beg = this->splits[0];
            }
            else {
                beg = this->splits[n - 1];
                if (n != r_size - 1) end = this->splits[n];
            }
        }

        // Performance optimization
        if (end == 0) {
            end = row_str.size();
        }
        
        return ret.substr(
            beg,
            end - beg // Number of characters
        );
    }

    /** @brief Return a CSVField object corrsponding to the nth value in the row.
     *
     *  This method performs boounds checking, and will throw an std::runtime_error
     *  if n is invalid.
     *
     *  @complexity Constant, by calling CSVRow::get_csv::string_view()
     *
     */
    CSVField CSVRow::operator[](size_t n) const {
        return CSVField(this->get_string_view(n));
    }

    /** @brief Retrieve a value by its associated column name. If the column
     *         specified can't be round, a runtime error is thrown.
     *
     *  @complexity Constant. This calls the other CSVRow::operator[]() after
                    converting column names into indices using a hash table.
     *
     *  @param[in] col_name The column to look for
     */
    CSVField CSVRow::operator[](const std::string& col_name) const {
        auto col_pos = this->col_names->col_pos.find(col_name);
        if (col_pos != this->col_names->col_pos.end())
            return this->operator[](col_pos->second);

        throw std::runtime_error("Can't find a column named " + col_name);
    }

    CSVRow::operator std::vector<std::string>() const {
        /** Convert this CSVRow into a vector of strings.
         *  **Note**: This is a less efficient method of
         *  accessing data than using the [] operator.
         */

        std::vector<std::string> ret;
        for (size_t i = 0; i < size(); i++)
            ret.push_back(std::string(this->get_string_view(i)));

        return ret;
    }

    //////////////////////
    // CSVField Methods //
    //////////////////////

    /**< @brief Return the type number of the stored value in
     *          accordance with the DataType enum
     */
    DataType CSVField::type() {
        this->get_value();
        return (DataType)_type;
    }

    #ifndef DOXYGEN_SHOULD_SKIP_THIS
    void CSVField::get_value() {
        /* Check to see if value has been cached previously, if not
         * evaluate it
         */
        if (_type < 0) {
            auto dtype = internals::data_type(this->sv, &this->value);
            this->_type = (int)dtype;
        }
    }
    #endif

    //
    // CSVField Utility Methods
    //

    bool CSVField::operator==(csv::string_view other) const {
        return other == this->sv;
    }

    bool CSVField::operator==(const long double& other) {
        return other == this->get<long double>();
    }

    /////////////////////
    // CSVRow Iterator //
    /////////////////////

    /** @brief Return an iterator pointing to the first field. */
    CSVRow::iterator CSVRow::begin() const {
        return CSVRow::iterator(this, 0);
    }

    /** @brief Return an iterator pointing to just after the end of the CSVRow.
     *
     *  Attempting to dereference the end iterator results in undefined behavior.
     */
    CSVRow::iterator CSVRow::end() const {
        return CSVRow::iterator(this, (int)this->size());
    }

    CSVRow::reverse_iterator CSVRow::rbegin() const {
        return std::reverse_iterator<CSVRow::iterator>(this->end());
    }

    CSVRow::reverse_iterator CSVRow::rend() const {
        return std::reverse_iterator<CSVRow::iterator>(this->begin());
    }

    CSVRow::iterator::iterator(const CSVRow* _reader, int _i)
        : daddy(_reader), i(_i) {
        if (_i < (int)this->daddy->size())
            this->field = std::make_shared<CSVField>(
                this->daddy->operator[](_i));
        else
            this->field = nullptr;
    }

    CSVRow::iterator::reference CSVRow::iterator::operator*() const {
        return *(this->field.get());
    }

    CSVRow::iterator::pointer CSVRow::iterator::operator->() const {
        // Using CSVField * as pointer type causes segfaults in MSVC debug builds
        #ifdef _MSC_BUILD
        return this->field;
        #else
        return this->field.get();
        #endif
    }

    CSVRow::iterator& CSVRow::iterator::operator++() {
        // Pre-increment operator
        this->i++;
        if (this->i < (int)this->daddy->size())
            this->field = std::make_shared<CSVField>(
                this->daddy->operator[](i));
        else // Reached the end of row
            this->field = nullptr;
        return *this;
    }

    CSVRow::iterator CSVRow::iterator::operator++(int) {
        // Post-increment operator
        auto temp = *this;
        this->operator++();
        return temp;
    }

    CSVRow::iterator& CSVRow::iterator::operator--() {
        // Pre-decrement operator
        this->i--;
        this->field = std::make_shared<CSVField>(
            this->daddy->operator[](this->i));
        return *this;
    }

    CSVRow::iterator CSVRow::iterator::operator--(int) {
        // Post-decrement operator
        auto temp = *this;
        this->operator--();
        return temp;
    }
    
    CSVRow::iterator CSVRow::iterator::operator+(difference_type n) const {
        // Allows for iterator arithmetic
        return CSVRow::iterator(this->daddy, i + (int)n);
    }

    CSVRow::iterator CSVRow::iterator::operator-(difference_type n) const {
        // Allows for iterator arithmetic
        return CSVRow::iterator::operator+(-n);
    }

    /** @brief Two iterators are equal if they point to the same field */
    bool CSVRow::iterator::operator==(const iterator& other) const {
        return this->i == other.i;
    }
}
#include <string>

namespace csv {
    /** @file
      * Calculates statistics from CSV files
      */

    CSVStat::CSVStat(std::string filename, CSVFormat format) :
        CSVReader(filename, format) {
        /** Lazily calculate statistics for a potentially large file. Once this constructor
         *  is called, CSVStat will process the entire file iteratively. Once finished,
         *  methods like get_mean(), get_counts(), etc... can be used to retrieve statistics.
         */
        while (!this->eof()) {
            this->read_csv("", internals::ITERATION_CHUNK_SIZE);
            this->calc();
        }

        if (!this->records.empty())
            this->calc();
    }

    void CSVStat::end_feed() {
        CSVReader::end_feed();
        this->calc();
    }

    /** @brief Return current means */
    std::vector<long double> CSVStat::get_mean() const {
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->col_names->size(); i++) {
            ret.push_back(this->rolling_means[i]);
        }
        return ret;
    }

    /** @brief Return current variances */
    std::vector<long double> CSVStat::get_variance() const {
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->col_names->size(); i++) {
            ret.push_back(this->rolling_vars[i]/(this->n[i] - 1));
        }
        return ret;
    }

    /** @brief Return current mins */
    std::vector<long double> CSVStat::get_mins() const {
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->col_names->size(); i++) {
            ret.push_back(this->mins[i]);
        }
        return ret;
    }

    /** @brief Return current maxes */
    std::vector<long double> CSVStat::get_maxes() const {
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->col_names->size(); i++) {
            ret.push_back(this->maxes[i]);
        }
        return ret;
    }

    /** @brief Get counts for each column */
    std::vector<CSVStat::FreqCount> CSVStat::get_counts() const {
        std::vector<FreqCount> ret;
        for (size_t i = 0; i < this->col_names->size(); i++) {
            ret.push_back(this->counts[i]);
        }
        return ret;
    }

    /** @brief Get data type counts for each column */
    std::vector<CSVStat::TypeCount> CSVStat::get_dtypes() const {
        std::vector<TypeCount> ret;        
        for (size_t i = 0; i < this->col_names->size(); i++) {
            ret.push_back(this->dtypes[i]);
        }
        return ret;
    }

    void CSVStat::calc() {
        /** Go through all records and calculate specified statistics */
        for (size_t i = 0; i < this->col_names->size(); i++) {
            dtypes.push_back({});
            counts.push_back({});
            rolling_means.push_back(0);
            rolling_vars.push_back(0);
            mins.push_back(NAN);
            maxes.push_back(NAN);
            n.push_back(0);
        }

        std::vector<std::thread> pool;

        // Start threads
        for (size_t i = 0; i < this->col_names->size(); i++)
            pool.push_back(std::thread(&CSVStat::calc_worker, this, i));

        // Block until done
        for (auto& th: pool)
            th.join();

        this->records.clear();
    }

    void CSVStat::calc_worker(const size_t &i) {
        /** Worker thread for CSVStat::calc() which calculates statistics for one column.
         * 
         *  @param[in] i Column index
         */

        auto current_record = this->records.begin();
        for (size_t processed = 0; current_record != this->records.end(); processed++) {
            auto current_field = (*current_record)[i];

            // Optimization: Don't count() if there's too many distinct values in the first 1000 rows
            if (processed < 1000 || this->counts[i].size() <= 500)
                this->count(current_field, i);

            this->dtype(current_field, i);

            // Numeric Stuff
            if (current_field.type() >= CSV_INT) {
                long double x_n = current_field.get<long double>();

                // This actually calculates mean AND variance
                this->variance(x_n, i);
                this->min_max(x_n, i);
            }

            ++current_record;
        }
    }

    void CSVStat::dtype(CSVField& data, const size_t &i) {
        /** Given a record update the type counter
         *  @param[in]  record Data observation
         *  @param[out] i      The column index that should be updated
         */
        
        auto type = data.type();
        if (this->dtypes[i].find(type) !=
            this->dtypes[i].end()) {
            // Increment count
            this->dtypes[i][type]++;
        } else {
            // Initialize count
            this->dtypes[i].insert(std::make_pair(type, 1));
        }
    }

    void CSVStat::count(CSVField& data, const size_t &i) {
        /** Given a record update the frequency counter
         *  @param[in]  record Data observation
         *  @param[out] i      The column index that should be updated
         */

        auto item = data.get<std::string>();

        if (this->counts[i].find(item) !=
            this->counts[i].end()) {
            // Increment count
            this->counts[i][item]++;
        } else {
            // Initialize count
            this->counts[i].insert(std::make_pair(item, 1));
        }
    }

    void CSVStat::min_max(const long double &x_n, const size_t &i) {
        /** Update current minimum and maximum
         *  @param[in]  x_n Data observation
         *  @param[out] i   The column index that should be updated
         */
        if (isnan(this->mins[i]))
            this->mins[i] = x_n;
        if (isnan(this->maxes[i]))
            this->maxes[i] = x_n;
        
        if (x_n < this->mins[i])
            this->mins[i] = x_n;
        else if (x_n > this->maxes[i])
            this->maxes[i] = x_n;
    }

    void CSVStat::variance(const long double &x_n, const size_t &i) {
        /** Given a record update rolling mean and variance for all columns
         *  using Welford's Algorithm
         *  @param[in]  x_n Data observation
         *  @param[out] i   The column index that should be updated
         */
        long double& current_rolling_mean = this->rolling_means[i];
        long double& current_rolling_var = this->rolling_vars[i];
        long double& current_n = this->n[i];
        long double delta;
        long double delta2;

        current_n++;
        
        if (current_n == 1) {
            current_rolling_mean = x_n;
        } else {
            delta = x_n - current_rolling_mean;
            current_rolling_mean += delta/current_n;
            delta2 = x_n - current_rolling_mean;
            current_rolling_var += delta*delta2;
        }
    }

    /** @brief Useful for uploading CSV files to SQL databases.
     *
     *  Return a data type for each column such that every value in a column can be
     *  converted to the corresponding data type without data loss.
     *  @param[in]  filename The CSV file
     *
     *  \return A mapping of column names to csv::DataType enums
     */
    std::unordered_map<std::string, DataType> csv_data_types(const std::string& filename) {
        CSVStat stat(filename);
        std::unordered_map<std::string, DataType> csv_dtypes;

        auto col_names = stat.get_col_names();
        auto temp = stat.get_dtypes();

        for (size_t i = 0; i < stat.get_col_names().size(); i++) {
            auto& col = temp[i];
            auto& col_name = col_names[i];

            if (col[CSV_STRING])
                csv_dtypes[col_name] = CSV_STRING;
            else if (col[CSV_LONG_LONG_INT])
                csv_dtypes[col_name] = CSV_LONG_LONG_INT;
            else if (col[CSV_LONG_INT])
                csv_dtypes[col_name] = CSV_LONG_INT;
            else if (col[CSV_INT])
                csv_dtypes[col_name] = CSV_INT;
            else
                csv_dtypes[col_name] = CSV_DOUBLE;
        }

        return csv_dtypes;
    }
}
#include <vector>


namespace csv {
    /**
     *  @brief Shorthand function for parsing an in-memory CSV string,
     *  a collection of CSVRow objects
     *
     *  \snippet tests/test_read_csv.cpp Parse Example
     *
     */
    CSVCollection parse(const std::string& in, CSVFormat format) {
        CSVReader parser(format);
        parser.feed(in);
        parser.end_feed();
        return parser.records;
    }

    /**
     * @brief Parse a RFC 4180 CSV string, returning a collection
     *        of CSVRow objects
     *
     * **Example:**
     *  \snippet tests/test_read_csv.cpp Escaped Comma
     *
     */
    CSVCollection operator ""_csv(const char* in, size_t n) {
        std::string temp(in, n);
        return parse(temp);
    }

    /**
     *  @brief Return a CSV's column names
     *
     *  @param[in] filename  Path to CSV file
     *  @param[in] format    Format of the CSV file
     *
     */
    std::vector<std::string> get_col_names(const std::string& filename, CSVFormat format) {
        CSVReader reader(filename, format);
        return reader.get_col_names();
    }

    /**
     *  @brief Find the position of a column in a CSV file or CSV_NOT_FOUND otherwise
     *
     *  @param[in] filename  Path to CSV file
     *  @param[in] col_name  Column whose position we should resolve
     *  @param[in] format    Format of the CSV file
     */
    int get_col_pos(
        const std::string filename,
        const std::string col_name,
        const CSVFormat format) {
        CSVReader reader(filename, format);
        return reader.index_of(col_name);
    }

    /** @brief Get basic information about a CSV file
     *  \include programs/csv_info.cpp
     */
    CSVFileInfo get_file_info(const std::string& filename) {
        CSVReader reader(filename);
        CSVFormat format = reader.get_format();
        for (auto& row : reader) {
            #ifndef NDEBUG
            SUPPRESS_UNUSED_WARNING(row);
            #endif
        }

        CSVFileInfo info = {
            filename,
            reader.get_col_names(),
            format.delim,
            reader.correct_rows,
            (int)reader.get_col_names().size()
        };

        return info;
    }
}
#include <cassert>


/** @file
 *  @brief Provides numeric parsing functionality
 */

namespace csv {
    namespace internals {
        #ifndef DOXYGEN_SHOULD_SKIP_THIS
        std::string type_name(const DataType& dtype) {
            switch (dtype) {
            case CSV_STRING:
                return "string";
            case CSV_INT:
                return "int";
            case CSV_LONG_INT:
                return "long int";
            case CSV_LONG_LONG_INT:
                return "long long int";
            case CSV_DOUBLE:
                return "double";
            default:
                return "null";
            }
        };
        #endif

        constexpr long double _INT_MAX = (long double)std::numeric_limits<int>::max();
        constexpr long double _LONG_MAX = (long double)std::numeric_limits<long int>::max();
        constexpr long double _LONG_LONG_MAX = (long double)std::numeric_limits<long long int>::max();

        /** Given a pointer to the start of what is start of 
         *  the exponential part of a number written (possibly) in scientific notation
         *  parse the exponent
         */
        inline DataType _process_potential_exponential(
            csv::string_view exponential_part,
            const long double& coeff,
            long double * const out) {
            long double exponent = 0;
            auto result = data_type(exponential_part, &exponent);

            if (result >= CSV_INT && result <= CSV_DOUBLE) {
                if (out) *out = coeff * pow10(exponent);
                return CSV_DOUBLE;
            }
            
            return CSV_STRING;
        }

        /** Given the absolute value of an integer, determine what numeric type 
         *  it fits in
         */
        inline DataType _determine_integral_type(const long double& number) {
            // We can assume number is always non-negative
            assert(number >= 0);

            if (number < _INT_MAX)
                return CSV_INT;
            else if (number < _LONG_MAX)
                return CSV_LONG_INT;
            else if (number < _LONG_LONG_MAX)
                return CSV_LONG_LONG_INT;
            else // Conversion to long long will cause an overflow
                return CSV_DOUBLE;
        }

        DataType data_type(csv::string_view in, long double* const out) {
            /** Distinguishes numeric from other text values. Used by various
             *  type casting functions, like csv_parser::CSVReader::read_row()
             *
             *  #### Rules
             *   - Leading and trailing whitespace ("padding") ignored
             *   - A string of just whitespace is NULL
             *
             *  @param[in] in String value to be examined
             */

            // Empty string --> NULL
            if (in.size() == 0)
                return CSV_NULL;

            bool ws_allowed = true,
                neg_allowed = true,
                dot_allowed = true,
                digit_allowed = true,
                has_digit = false,
                prob_float = false;

            unsigned places_after_decimal = 0;
            long double integral_part = 0,
                decimal_part = 0;

            for (size_t i = 0, ilen = in.size(); i < ilen; i++) {
                const char& current = in[i];

                switch (current) {
                case ' ':
                    if (!ws_allowed) {
                        if (isdigit(in[i - 1])) {
                            digit_allowed = false;
                            ws_allowed = true;
                        }
                        else {
                            // Ex: '510 123 4567'
                            return CSV_STRING;
                        }
                    }
                    break;
                case '-':
                    if (!neg_allowed) {
                        // Ex: '510-123-4567'
                        return CSV_STRING;
                    }

                    neg_allowed = false;
                    break;
                case '.':
                    if (!dot_allowed) {
                        return CSV_STRING;
                    }

                    dot_allowed = false;
                    prob_float = true;
                    break;
                case 'e':
                case 'E':
                    // Process scientific notation
                    if (prob_float) {
                        size_t exponent_start_idx = i + 1;

                        // Strip out plus sign
                        if (in[i + 1] == '+') {
                            exponent_start_idx++;
                        }

                        return _process_potential_exponential(
                            in.substr(exponent_start_idx),
                            neg_allowed ? integral_part + decimal_part : -(integral_part + decimal_part),
                            out
                        );
                    }

                    return CSV_STRING;
                    break;
                default:
                    if (isdigit(current)) {
                        // Process digit
                        has_digit = true;

                        if (!digit_allowed)
                            return CSV_STRING;
                        else if (ws_allowed) // Ex: '510 456'
                            ws_allowed = false;

                        // Build current number
                        unsigned digit = current - '0';
                        if (prob_float) {
                            decimal_part += digit / pow10(++places_after_decimal);
                        }
                        else {
                            integral_part = (integral_part * 10) + digit;
                        }
                    }
                    else {
                        return CSV_STRING;
                    }
                }
            }

            // No non-numeric/non-whitespace characters found
            if (has_digit) {
                long double number = integral_part + decimal_part;
                if (out) {
                    *out = neg_allowed ? number : -number;
                }

                return prob_float ? CSV_DOUBLE : _determine_integral_type(number);
            }

            // Just whitespace
            return CSV_NULL;
        }
    }
}

namespace csv {
    namespace internals {
        /**
         * Return a string_view over the current_row
         */
        csv::string_view GiantStringBuffer::get_row() {
            csv::string_view ret(
                this->buffer->c_str() + this->current_end, // Beginning of string
                (this->buffer->size() - this->current_end) // Count
            );

            this->current_end = this->buffer->size();
            return ret;
        }

        /** Return size of current row */
        size_t GiantStringBuffer::size() const {
            return (this->buffer->size() - this->current_end);
        }

        std::string* GiantStringBuffer::get() const {
            return this->buffer.get();
        }

        std::string* GiantStringBuffer::operator->() const {
            return this->buffer.operator->();
        }
        
        /** Clear out the buffer, but save current row in progress */
        void GiantStringBuffer::reset() {
            // Save current row in progress
            auto temp_str = this->buffer->substr(
                this->current_end,   // Position
                (this->buffer->size() - this->current_end) // Count
            );

            this->current_end = 0;
            this->buffer = std::make_shared<std::string>(temp_str);
        }
    }
}

