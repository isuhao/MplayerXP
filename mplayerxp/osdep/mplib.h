/*
 *  mplib.h
 *
 *	Copyright (C) Nickols_K <nickols_k@mail.ru> - Oct 2001
 *
 *  You can redistribute this file under terms and conditions
 *  of GNU General Public licence v2.
 */
#ifndef __MPLIB_H_INCLUDED__
#define __MPLIB_H_INCLUDED__ 1
#include <string>
#include <exception>

#include <execinfo.h>
#include <stddef.h>
#include <sys/mman.h>
#include "mpxp_config.h"
#include "mp_malloc.h"

namespace	usr {

    enum mplayerxp_data_type_qualifier__byte_t{ type_byte=0 };
    enum mplayerxp_data_type_qualifier__word_t{ type_word=0 };
    enum mplayerxp_data_type_qualifier_dword_t{ type_dword=0 };
    enum mplayerxp_data_type_qualifier_qword_t{ type_qword=0 };

    class Opaque {
	public:
	    Opaque();
	    virtual ~Opaque();
	
	any_t*		false_pointers[RND_CHAR0];
	any_t*		unusable;
    };

    template <typename T> class LocalPtr : public Opaque {
	public:
	    LocalPtr(T* value):ptr(value) {}
	    virtual ~LocalPtr() { delete ptr; }

	    T& operator*() const { return *ptr; }
	    T* operator->() const { return ptr; }
	private:
	    LocalPtr<T>& operator=(LocalPtr<T> a) { return this; }
	    LocalPtr<T>& operator=(LocalPtr<T>& a) { return this; }
	    LocalPtr<T>& operator=(LocalPtr<T>* a) { return this; }

	    Opaque	opaque1;
	    T* ptr;
	    Opaque	opaque2;
    };

    class bad_format_exception : public std::exception {
	public:
	    bad_format_exception() throw();
	    virtual ~bad_format_exception() throw();

	    virtual const char*	what() const throw();
    };

    class missing_driver_exception : public std::exception {
	public:
	    missing_driver_exception() throw();
	    virtual ~missing_driver_exception() throw();

	    virtual const char*	what() const throw();
    };

    class soft_exit_exception : public std::exception {
	public:
	    soft_exit_exception(const std::string& why) throw();
	    virtual ~soft_exit_exception() throw();

	    virtual const char*	what() const throw();
	private:
	    std::string why;
    };
} // namespace	usr
#endif
