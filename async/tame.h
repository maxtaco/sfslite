
// -*-c++-*-
/* $Id$ */

/*
 *
 * Copyright (C) 2005 Max Krohn (max@okws.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 */

#ifndef _ASYNC_TAME_H
#define _ASYNC_TAME_H


/*
 * async/tame.h
 *
 *   Runtime classes, constants, and MACROs for libasync files that have
 *   been 'tame'd by the tame utility.  That is, all files that have been
 *   translated by tame should #include this file.
 *
 *   Not to be confused with tame/tame.h, which is read only when sfslite
 *   is being compile, and used to make the tame binary.  That tame
 *   (tame/tame.h) is not installed in /usr/local/include..., but this
 *   one is.
 *
 *   The main classes defined are the closure_t baseclass for all
 *   autogenerated closures, and various join_group_t's, used for
 *   NONBLOCK and JOIN constructs.
 *
 */

#include "async.h"
#include "qhash.h"
#include "keyfunc.h"

/**
 * not in use, but perhaps useful for "zero"ing out callback::ref's,
 * which might be necessary.
 */
template<class T>
class generic_wrapper_t {
public:
  generic_wrapper_t (T o) : _obj (o) {}
  T obj () const { return _obj; }
private:
  const T _obj;
};

/**
 * Weak Reference Counting
 *
 *   Here follows some rudimentary support for 'weak reference counting' 
 *   or WRC for short. The main idea behind WRC is that it is possible
 *   to hold a pointer to an object, and know whether or not it has 
 *   gone out of scope, without keeping it in scope as asycn/refcnt.h
 *   would do.
 *
 *   A class XX that wants to be weak refcounted should inheret from
 *   weak_refcounted_t<XX>.  This will give XX a refcounted bool, which
 *   will be set to true once an object of type XX is destroyed.  References
 *   to that object will hold a reference to this boolean flag, and can
 *   therefore tell when the XX object has gone out of scope.
 *
 *   Given an object xx of class XX that inherets from weak_refcounted_t<XX>, 
 *   get a weak reference to xx by calling weak_ref_t<XX> (&xx).
 *
 *   One possible use for this system is to keep track of what an object's
 *   scope ought to be with a weak_refcounted_t.  Once can incref and
 *   decref such an object as it gains and loses references.  The object
 *   itself is kept in conservatively in scope, though, with true reference 
 *   counting, to avoid memory corruption.  When there is a disagreement
 *   between the weak reference counting and the real reference counting
 *   as to when the object went of out scope, then a warning message
 *   can be generated.
 */

template<class T> class weak_ref_t;

class mortal_t;
class mortal_ref_t {
public:
  mortal_ref_t (mortal_t *m, ptr<bool> d1, ptr<bool> d2)
    : _mortal (m),
      _destroyed_flag (d1),
      _dead_flag (d2) {}

  void mark_dead ();
private:
  mortal_t *_mortal;
  ptr<bool> _destroyed_flag, _dead_flag;
};

/**
 * things can be separately marked dead and destroyed.
 */
class mortal_t {
public:
  mortal_t () : 
    _destroyed_flag (New refcounted<bool> (false)),
    _dead_flag (New refcounted<bool> (false))
  {}

  virtual ~mortal_t () { *_destroyed_flag = true; }
  virtual void mark_dead () {}
  ptr<bool> destroyed_flag () { return _destroyed_flag; }
  ptr<bool> dead_flag () { return _dead_flag; }

  mortal_ref_t make_mortal_ref () 
  { return mortal_ref_t (this, _destroyed_flag, _dead_flag); }

protected:
  ptr<bool> _destroyed_flag, _dead_flag;
};


/**
 * A class XX that wants to be weak_refcounted should inherit from
 * weak_refcounted_t<XX>.
 */
template<class T>
class weak_refcounted_t : public mortal_t {
public:
  weak_refcounted_t (T *p) :
    _pointer (p),
    _refcnt (1) {}

  virtual ~weak_refcounted_t () {}

  /**
   * eschew fancy C++ cast operator overloading for ease or readability, etc..
   */
  T * pointer () { return _pointer; }

  /**
   * Make a weak reference to this class, copying over the base pointer,
   * and also the refcounted bool that represents whether or not this
   * class is in scope.
   */
  weak_ref_t<T> make_weak_ref ();
  
  /**
   * One can manually manage the reference count of weak references.
   * When an object weakly leaves scope, it doesn't do anything real
   * (such as deleting an object or setting the _destroyed flag).
   * Rather, it can report an error if a callback for that style of
   * error report was requested.
   */
  void weak_incref () { _refcnt++; }
  void weak_decref () 
  {
    assert ( --_refcnt >= 0);
    if (!_refcnt) {
      cbv::ptr c = _weak_finalize_cb;
      _weak_finalize_cb = NULL;
      if (c) 
	delaycb (0,0,c);
    }
  }
  void set_weak_finalize_cb (cbv::ptr c) { _weak_finalize_cb = c; }
  
private:
  T        *_pointer;
  int _refcnt;
  cbv::ptr _weak_finalize_cb;
};

/**
 * Weak reference: hold a pointer to an object, without increasing its
 * refcount, but still knowing whether or not it's been destroyed.
 * If destroyed, don't access it!
 *
 * @param T a class that implements the method ptr<bool> destroyed_flag()
 */
template<class T>
class weak_ref_t {
public:
  weak_ref_t (weak_refcounted_t<T> *p) : 
    _pointer (p->pointer ()), 
    _destroyed_flag (p->destroyed_flag ()) {}

  /**
   * Access the underlying pointer only after we have ensured that
   * that object it points to is still in scope.
   */
  T * pointer () { return (*_destroyed_flag ? NULL : _pointer); }

  void weak_decref () { if (pointer ()) pointer ()->weak_decref (); }
  void weak_incref () { if (pointer ()) pointer ()->weak_incref (); }

private:
  T                            *_pointer;
  ptr<bool>                    _destroyed_flag;
};

template<class T> weak_ref_t<T>
weak_refcounted_t<T>::make_weak_ref ()
{
  return weak_ref_t<T> (this);
}

// All closures are numbered serially so that our accounting does not
// get confused.
extern u_int64_t closure_serial_number;

class closure_t : public virtual refcount , 
		  public weak_refcounted_t<closure_t>
{
public:
  closure_t (bool c = false) : 
    weak_refcounted_t<closure_t> (this),
    _jumpto (0), 
    _cceoc_count (0),
    _has_cceoc (c),
    _id (++closure_serial_number)
  {}

  // because it is weak_refcounted, a bool will turn to true once
  // this class is deleted.
  ~closure_t () {}

  virtual bool is_onstack (const void *p) const = 0;

  // manage function reentry
  void set_jumpto (int i) { _jumpto = i; }
  u_int jumpto () const { return _jumpto; }

  // "Call-Exactly-Once Checked Continuation"
  void inc_cceoc_count () { _cceoc_count ++; }
  void set_has_cceoc (bool f) { _has_cceoc = f; }
  void enforce_cceoc (const str &loc);

  u_int64_t id () { return _id; }

  // given a file/line number of the end of scope, perform sanity
  // checks on scoping, etc.
  void end_of_scope_checks (str loc);

  // Associate a join group with this closure
  void associate_join_group (mortal_ref_t mref, const void *jgwp);

  // Account for join groups that **should** be going out of
  // scope as we are weakly going out of scope
  void kill_join_groups ();

protected:
  u_int _jumpto;
  int _cceoc_count;
  bool _has_cceoc;
  u_int64_t _id;

  // All of the join groups that are within our bounds
  vec<mortal_ref_t> _join_groups;
};

template<class T1 = int, class T2 = int, class T3 = int, class T4 = int>
struct value_set_t {
  value_set_t () {}
  value_set_t (T1 v1) : v1 (v1) {}
  value_set_t (T1 v1, T2 v2) : v1 (v1), v2 (v2) {}
  value_set_t (T1 v1, T2 v2, T3 v3) : v1 (v1), v2 (v2), v3 (v3) {}
  value_set_t (T1 v1, T2 v2, T3 v3, T4 v4) 
    : v1 (v1), v2 (v2), v3 (v3), v4 (v4) {}

  T1 v1;
  T2 v2;
  T3 v3;
  T4 v4;
};

/**
 * functions defined in tame.C, mainly for reporting errors, and
 * determinig what will happen when an error occurs. Change the
 * runtime behavior of what happens in an error via TAME_OPTIONS
 */
void tame_error (const str &loc, const str &msg);
INIT(tame_init);

/**
 * Holds the important information associated with a join group,
 * such as how many calls are oustanding, and who is waiting to join.
 * Therefore has the callbacks to match joiners up with those waiting
 * to join.
 */
template<class T1 = int, class T2 = int, class T3 = int, class T4 = int>
class join_group_pointer_t 
  : public virtual refcount ,
    public weak_refcounted_t<join_group_pointer_t<T1,T2,T3,T4> >
{
public:
  join_group_pointer_t (const char *f, int l) : 
    weak_refcounted_t<join_group_pointer_t<T1,T2,T3,T4> > (this),
    _n_out (0), _file (f), _lineno (l)  {}

  virtual ~join_group_pointer_t () { mark_dead (); }

  void mark_dead ()
  { 
    if (*mortal_t::dead_flag ())
      return;
    (*mortal_t::dead_flag ()) = true;

    // XXX - find some way to identify this join, either by filename
    // and line number, or other ways.
    if (need_join ()) {
      str s = (_file && _lineno) ?
	str (strbuf ("%s:%d", _file, _lineno)) :
	str ("(unknown)");
      tame_error (s, "non-joined continuations leaked!"); 
    }

    // unregister all closures by weakly decref'ing them
    for (u_int i = 0; i < _closures.size (); i++)
      _closures[i].weak_decref ();
  }

  void associate_closure (ptr<closure_t> c, void *jgwp) 
  {
    // make sure that each closure is registered only once!
    u_int64_t p = c->id ();
    if (!_closure_bhash[p]) {
      _closure_bhash.insert (p);
      weak_ref_t<closure_t> wr = c->make_weak_ref ();
      wr.weak_incref ();
      _closures.push_back (wr);

      // make a two-way association
      //
      // jgwp = join group wrapper pointer; since the join group
      // and not the associated join_group_pointer is going to be
      // allocated in VARS{}, we need to track its memory location,
      // to see if it was allocated inside of the closure.  If so,
      // we need to force it out of scope when we force the closure
      // out of scope.
      c->associate_join_group (mortal_t::make_mortal_ref (), jgwp);
    }
  }

  void set_join_cb (cbv::ptr c) { _join_cb = c; }

  void launch_one (ptr<closure_t> c = NULL, void *jgwp = NULL) 
  { 
    if (c)
      associate_closure (c, jgwp);
    add_join ();
  }

  void add_join () { _n_out ++; }
  void remove_join () { assert (_n_out-- > 0);}

  void join (value_set_t<T1, T2, T3, T4> v) 
  {
    _n_out --;
    _pending.push_back (v);
    if (_join_cb) {
      cbv cb = _join_cb;
      _join_cb = NULL;
      (*cb) ();
    }
  }

  u_int n_pending () const { return _pending.size (); }
  u_int n_out () const { return _n_out; }
  u_int n_joins_left () const { return _n_out + _pending.size (); }
  bool need_join () const { return n_joins_left () > 0; }

  static value_set_t<T1,T2,T3,T4> to_vs ()
  { return value_set_t<T1,T2,T3,T4> (); }

  bool pending (value_set_t<T1, T2, T3, T4> *p)
  {
    bool ret = false;
    if (_pending.size ()) {
      *p = _pending.pop_front ();
      ret = true;
    }
    return ret;
  }

private:
  // number of calls out that haven't yet completed
  u_int _n_out; 

  // number of completing calls that are waiting for a join call
  vec<value_set_t<T1, T2, T3, T4> > _pending;

  // callback to call once
  cbv::ptr _join_cb;

  // try to debug leaked joiners
  const char *_file;
  int _lineno;

  // keep weak references to all of the closures that we references;
  // when we go out of scope, we will unregister at those closures,
  // so the closures can detect closure leaks!
  bhash<u_int>                _closure_bhash;
  vec<weak_ref_t<closure_t> > _closures;
};

/**
 * Joiners can theoretically persist past when the join group went
 * out of scope; they shouldn't keep join groups in scope just because
 * they haven't fired yet -- this would lead to memory leaks.  Therefore,
 * a joiner holds a *weak reference* to a join group.  If the join group
 * went out of strong scope before we had a choice to join, there was
 * definitely a problem, and we report it.
 */
template<class T1 = int, class T2 = int, class T3 = int, class T4 = int>
class joiner_t : public virtual refcount {
public:
  joiner_t (ptr<join_group_pointer_t<T1,T2,T3,T4> > p, const str &l) 
    : _weak_ref (p->make_weak_ref ()), _loc (l) {}

  void join (value_set_t<T1,T2,T3,T4> w)
  {
    delaycb (0, 0, wrap (mkref (this), &joiner_t<T1,T2,T3,T4>::join_cb, w));
  }
  
private:
  void error ()
  {
    tame_error (_loc, "join_group went out of scope");
  }

  // cannot join if the underlying join group went out of scope.
  void join_cb (value_set_t<T1,T2,T3,T4> w)
  {
    if (!_weak_ref.pointer ()) {
      error ();
    } else {
      _weak_ref.pointer ()->join (w);
    }
  }

  weak_ref_t<join_group_pointer_t<T1,T2,T3,T4> > _weak_ref;
  str _loc;
};

/**
 * @brief A wrapper class around a join group pointer for tighter code
 */
template<class T1 = int, class T2 = int, class T3 = int, class T4 = int>
class join_group_t {
public:
  join_group_t (const char *f = NULL, int l = 0) 
    : _pointer (New refcounted<join_group_pointer_t<T1,T2,T3,T4> > (f, l)) {}
  join_group_t (ptr<join_group_pointer_t<T1,T2,T3,T4> > p) : _pointer (p) {}

  //-----------------------------------------------------------------------
  //
  // The following functions are used by the rewriter, but can also
  // be accessed by the programmer when manipulating join groups
  //

  /**
   * Register another outstanding callback to join; useful in the case
   * of "sticky" callbacks the fire more than once.  If you expect a 
   * callback generated with '@' to call more than once, call 'add_join'
   * for all but the first callback fire.
   */
  void add_join () { _pointer->add_join (); }

  /**
   * Unregister a callback, in case a callback was canceled before it
   * fired.
   */
  void remove_join () { _pointer->remove_join (); }

  /**
   * Determine the number of callbacks that have fired, but are waiting
   * for a join.
   */
  u_int n_pending () const { return _pointer->n_pending (); }

  /**
   * Determine the number of callbacks still out, that have yet to fire.
   */
  u_int n_out () const { return _pointer->n_out (); }

  /**
   * Determine the total number of joins left to do before the join group
   * has been drained.  Number of joins left to do is determined by adding
   * the number of callbacks left to fire to the number of callbacks 
   * already fired, and waiting for a join.
   */
  u_int n_joins_left () const { return _pointer->n_joins_left (); }

  /**
   * On if an additional join is needed.
   */
  bool need_join () const { return _pointer->need_join (); }

  /**
   * Get the next pending event; returns true and gives the values of that
   * event if there is, in fact, a callback that has fired, but hasn't
   * been joined on yet.  Returns false if there are no pending events
   * to join on.
   */
  bool pending (value_set_t<T1, T2, T3, T4> *p)
  { return _pointer->pending (p); }

  //
  // end of public functions that the programmer should call
  //-----------------------------------------------------------------------

  //-----------------------------------------------------------------------
  //  
  // The following functions are public because the need to be accessed
  // by the tame rewriter; they should not be called directly by 
  // programmers.
  void set_join_cb (cbv::ptr c) { _pointer->set_join_cb (c); }
  void launch_one (ptr<closure_t> c = NULL) 
  { _pointer->launch_one (c, static_cast<void *> (this)); }

  ptr<join_group_pointer_t<T1,T2,T3,T4> > pointer () { return _pointer; }
  static value_set_t<T1,T2,T3,T4> to_vs () 
  { return value_set_t<T1,T2,T3,T4> (); }

  ptr<joiner_t<T1,T2,T3,T4> > make_joiner (const str &loc) 
  { return New refcounted<joiner_t<T1,T2,T3,T4> > (_pointer, loc); }

  //
  //-----------------------------------------------------------------------

private:
  ptr<join_group_pointer_t<T1,T2,T3,T4> > _pointer;
};


template<class T1>
struct pointer_set1_t {
  pointer_set1_t (T1 *p1) : p1 (p1) {}
  T1 *p1;
};


template<class T1, class T2>
struct pointer_set2_t {
  pointer_set2_t (T1 *p1, T2 *p2) : p1 (p1), p2 (p2) {}
  T1 *p1;
  T2 *p2;
};

template<class T1, class T2, class T3>
struct pointer_set3_t {
  pointer_set3_t (T1 *p1, T2 *p2, T3 *p3) : p1 (p1), p2 (p2), p3 (p3) {}
  T1 *p1;
  T2 *p2;
  T3 *p3;
};

template<class T1, class T2, class T3, class T4>
struct pointer_set4_t {
  pointer_set4_t (T1 *p1, T2 *p2, T3 *p3, T4 *p4) 
    : p1 (p1), p2 (p2), p3 (p3), p4 (p4) {}
  T1 *p1;
  T2 *p2;
  T3 *p3;
  T4 *p4;
};

template<class T> void use_reference (T &i) {}

// make shortcuts to the most common callbacks, but while using
// ptr's, and not ref's.
typedef callback<void>::ptr ceo_callback_void_t;
typedef callback<void, int>::ptr ceo_callback_int_t;
typedef callback<void, str>::ptr ceo_callback_str_t;
typedef callback<void, bool>::ptr ceo_callback_bool_t;

#define TAME_GLOBAL_INT      tame_global_int
#define CLOSURE              ptr<closure_t> __frame = NULL
#define TAME_OPTIONS         "TAME_OPTIONS"
#define __CLS                 __cls     
#define CCEOC_STACK_SENTINEL  _cceoc_not_called_if_uninitialzed

extern int TAME_GLOBAL_INT;

/**
 * Set the cceoc variable to be unitialized
 */
#define SET_CCEOC_STACK_SENTINEL()  do { CCEOC_STACK_SENTINEL = 0; } while(0)


/**
 * At the end of scope, perform both static and dynamic checks on
 * the CCEOC.
 */
#define END_OF_SCOPE(x)                                           \
do {                                                              \
  TAME_GLOBAL_INT = CCEOC_STACK_SENTINEL;                         \
  __CLS->end_of_scope_checks (x);                                 \
} while (0)

/**
 * Macro to call to unblock the calling function; but does *not* 
 * return to the caller.  Note that CCEOCs are destroyed after they
 * are used, so clean up wrapped-in closures from caller stack frames.
 */
#define UNBLOCK(x, T, ...)                                        \
do {                                                              \
  __CLS->inc_cceoc_count ();                                      \
  SET_CCEOC_STACK_SENTINEL();                                     \
  const T cb_tmp (CCEOC_ARGNAME);                                 \
  CCEOC_ARGNAME = NULL;                                           \
  (*cb_tmp) (__VA_ARGS__);                                        \
} while (0)

/**
 * Like unblock, but return from the function.
 */
#define RESUME(x, T, ...)                                         \
do {                                                              \
  UNBLOCK(x, T, __VA_ARGS__);                                     \
  END_OF_SCOPE(x);                                                \
  return;                                                         \
} while (0)

// Tame template type
#define TTT(x) typeof(UNREF(typeof(x)))

template<class T1>
void __block_cb1 (pointer_set1_t<T1> p, cbv cb, T1 v1)
{
  *p.p1 = v1;
  (*cb) ();
}

template<class T1, class T2>
void __block_cb2 (pointer_set2_t<T1,T2> p, cbv cb, T1 v1, T2 v2)
{
  *p.p1 = v1;
  *p.p2 = v2;
  (*cb) ();
}
  
template<class T1, class T2, class T3>
void __block_cb3 (pointer_set3_t<T1,T2,T3> p, cbv cb, T1 v1, T2 v2, T3 v3)
{
  *p.p1 = v1;
  *p.p2 = v2;
  *p.p3 = v3;
  (*cb) ();
}

template<class T1, class T2, class T3, class T4>
void __block_cb4 (pointer_set4_t<T1,T2,T3,T4> p, cbv cb,
		  T1 v1, T2 v2, T3 v3, T4 v4)
{
  *p.p1 = v1;
  *p.p2 = v2;
  *p.p3 = v3;
  *p.p4 = v4;
  (*cb) ();
}


#endif /* _ASYNC_TAME_H */
