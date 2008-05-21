#include "prelude.hpp"
#include "object.hpp"
#include "objects.hpp"
#include "vm.hpp"
#include "objectmemory.hpp"

#include <ctype.h>
#include <math.h>
#include "tommath.h"

#define BASIC_CLASS(blah) G(blah)
#define NEW_STRUCT(obj, str, kls, kind) \
  obj = (typeof(obj))state->new_struct(kls, sizeof(kind)); \
  str = (kind *)(obj->bytes)
#define DATA_STRUCT(obj, type) ((type)(obj->bytes))

#define NMP mp_int *n; Bignum* n_obj; \
  NEW_STRUCT(n_obj, n, BASIC_CLASS(bignum), mp_int); \
  mp_init(n);
#define MMP mp_int *m; Bignum* m_obj; \
  NEW_STRUCT(m_obj, m, BASIC_CLASS(bignum), mp_int); \
  mp_init(m);


#define MP(k) DATA_STRUCT(k, mp_int*)
#define BDIGIT_DBL long long
#define DIGIT_RADIX (1L << DIGIT_BIT)

namespace rubinius {

  static void twos_complement(mp_int *a)
  {
    long i = a->used;
    BDIGIT_DBL num;

    while (i--) {
      DIGIT(a,i) = (~DIGIT(a,i)) & (DIGIT_RADIX-1);
    }

    i = 0; num = 1;
    do {
      num += DIGIT(a,i);
      DIGIT(a,i++) = num & (DIGIT_RADIX-1);
      num = num >> DIGIT_BIT;
    } while (i < a->used);

  }

#define BITWISE_OP_AND 1
#define BITWISE_OP_OR  2
#define BITWISE_OP_XOR 3

  static void bignum_bitwise_op(int op, mp_int *x, mp_int *y, mp_int *n)
  {
    mp_int   a,   b;
    mp_int *d1, *d2;
    int i, sign,  l1,  l2;
    mp_init(&a) ; mp_init(&b);

    if (y->sign == MP_NEG) {
      mp_copy(y, &b);
      twos_complement(&b);
      y = &b;
    }

    if (x->sign == MP_NEG) {
      mp_copy(x, &a);
      twos_complement(&a);
      x = &a;
    }

    if (x->used > y->used) {
      l1 = y->used;
      l2 = x->used;
      d1 = y;
      d2 = x;
      sign = y->sign;
    } else {
      l1 = x->used;
      l2 = y->used;
      d1 = x;
      d2 = y;
      sign = x->sign;
    }

    mp_grow(n, l2);
    n->used = l2;
    n->sign = MP_ZPOS;
    switch(op) {
      case BITWISE_OP_AND:
        if (x->sign == MP_NEG && y->sign == MP_NEG) n->sign = MP_NEG;
        for (i=0; i < l1; i++) {
          DIGIT(n,i) = DIGIT(d1,i) & DIGIT(d2,i);
        }
        for (; i < l2; i++) {
          DIGIT(n,i) = (sign == MP_ZPOS)?0:DIGIT(d2,i);
        }
        break;
      case BITWISE_OP_OR:
        if (x->sign == MP_NEG || y->sign == MP_NEG) n->sign = MP_NEG;
        for (i=0; i < l1; i++) {
          DIGIT(n,i) = DIGIT(d1,i) | DIGIT(d2,i);
        }
        for (; i < l2; i++) {
          DIGIT(n,i) = (sign == MP_ZPOS)?DIGIT(d2,i):(DIGIT_RADIX-1);
        }
        break;
      case BITWISE_OP_XOR:
        if (x->sign != y->sign) n->sign = MP_NEG;
        for (i=0; i < l1; i++) {
          DIGIT(n,i) = DIGIT(d1,i) ^ DIGIT(d2,i);
        }
        for (; i < l2; i++) {
          DIGIT(n,i) = (sign == MP_ZPOS)?DIGIT(d2,i):~DIGIT(d2,i);
        }
        break;
    }

    if (n->sign == MP_NEG) twos_complement(n);

    /* free allocated resources for twos complement copies */
    mp_clear(&a);
    mp_clear(&b);
  }

  void Bignum::Info::cleanup(OBJECT obj) {
    Bignum* big = as<Bignum>(obj);
    mp_int *n = MP(big);
    mp_clear(n);
  }

  void Bignum::Info::mark(OBJECT obj, ObjectMark& mark) { }

  void Bignum::init(STATE) {
    state->add_type_info(new Bignum::Info(Bignum::type));
  }

  Bignum* Bignum::create(STATE, native_int num) {
    mp_int *a;
    Bignum* o;
    o = (Bignum*)state->new_struct(G(bignum), sizeof(mp_int));
    a = (mp_int*)(o->bytes);

    if(num < 0) {
      mp_init_set_int(a, (unsigned long)-num);
      a->sign = MP_NEG;
    } else {
      mp_init_set_int(a, (unsigned long)num);
    }
    return o;
  }

  Bignum* Bignum::new_unsigned(STATE, unsigned int num) {
    Bignum* o;
    o = (Bignum*)state->new_struct(G(bignum), sizeof(mp_int));
    mp_init_set_int(MP(o), num);
    return o;
  }

  INTEGER Bignum::normalize(STATE, Bignum* b) {
    mp_clamp(MP(b));

    if((size_t)mp_count_bits(MP(b)) <= FIXNUM_WIDTH) {
      native_int val;
      val = (native_int)b->to_ll(state);
      return Object::i2n(val);
    }
    return b;
  }

  INTEGER Bignum::add(STATE, FIXNUM b) {
    NMP;
    native_int bi = b->n2i();
    if(bi > 0) {
      mp_add_d(MP(this), bi, n);
    } else {
      mp_sub_d(MP(this), -bi, n);
    }
    return Bignum::normalize(state, n_obj);
  }

  INTEGER Bignum::add(STATE, Bignum* b) {
    NMP;
    mp_add(MP(this), MP(b), n);
    return Bignum::normalize(state, n_obj);
  }

  Float* Bignum::add(STATE, Float* b) {
    return b->add(state, this);
  }

  INTEGER Bignum::sub(STATE, FIXNUM b) {
    NMP;
    native_int bi = b->n2i();
    if(bi > 0) {
      mp_sub_d(MP(this), bi, n);
    } else {
      mp_add_d(MP(this), -bi, n);
    }
    return Bignum::normalize(state, n_obj);
  }

  INTEGER Bignum::sub(STATE, Bignum* b) {
    NMP;
    mp_sub(MP(this), MP(b), n);
    return Bignum::normalize(state, n_obj);
  }

  Float* Bignum::sub(STATE, Float* b) {
    return Float::coerce(state, this)->sub(state, b);
  }

  void Bignum::debug(STATE) {
    char *stra = (char*)XMALLOC(2048);
    mp_toradix(MP(this), stra, 10);
    printf("n: %s\n", stra);
    FREE(stra);
    return;
  }

  INTEGER Bignum::mul(STATE, FIXNUM b) {
    NMP;

    native_int bi = b->n2i();
    if(bi == 2) {
      mp_mul_2(MP(this), n);
    } else {
      if(bi > 0) {
        mp_mul_d(MP(this), bi, n);
      } else {
        mp_mul_d(MP(this), -bi, n);
        mp_neg(n, n);
      }
    }
    return Bignum::normalize(state, n_obj);
  }

  INTEGER Bignum::mul(STATE, Bignum* b) {
    NMP;
    mp_mul(MP(this), MP(b), n);
    return Bignum::normalize(state, n_obj);
  }

  Float* Bignum::mul(STATE, Float* b) {
    return b->mul(state, this);
  }

  INTEGER Bignum::divide(STATE, FIXNUM denominator, INTEGER* remainder) {
    NMP;

    native_int bi  = denominator->n2i();
    mp_digit r;
    if(bi < 0) {
      mp_div_d(MP(this), -bi, n, &r);
      mp_neg(n, n);
    } else {
      mp_div_d(MP(this), bi, n, &r);
    }
    
    if(remainder) {
      if(MP(this)->sign == MP_NEG) {
        *remainder = Object::i2n(-(native_int)r);
      } else {
        *remainder = Object::i2n((native_int)r);
      }
    }
    
    if(mp_cmp_d(n, 0) == MP_LT && r != 0) {
      if(remainder) {
        *remainder = Object::i2n(as<Fixnum>(*remainder)->n2i() + bi);
      }
      mp_int m;
      mp_init(&m);
      mp_sub_d(n, 1, &m);
      mp_copy(&m, n);
      mp_clear(&m);
    }
    return Bignum::normalize(state, n_obj);
  }

  INTEGER Bignum::divide(STATE, Bignum* b, INTEGER* remainder) {
    NMP;
    MMP;
    mp_div(MP(this), MP(b), n, m);
    if(mp_cmp_d(n, 0) == MP_LT && mp_cmp_d(m, 0) != MP_EQ) {
      mp_sub_d(n, 1, m);
      mp_copy(m, n);
      mp_mul(MP(b), n, m);
      mp_sub(MP(this), m, m);
    }
    if(remainder) {
      *remainder = Bignum::normalize(state, m_obj);
    }
    return Bignum::normalize(state, n_obj);
  }

  Array* Bignum::divmod(STATE, FIXNUM denominator) {
    INTEGER mod = Object::i2n(0);
    INTEGER quotient = divide(state, denominator, &mod);
    
    Array* ary = Array::create(state, 2);
    ary->set(state, 0, quotient);
    ary->set(state, 1, mod);
    
    return ary;
  }

  Array* Bignum::divmod(STATE, Bignum* denominator) {
    INTEGER mod = Object::i2n(0);
    INTEGER quotient = divide(state, denominator, &mod);
    Array* ary = Array::create(state, 2);
    ary->set(state, 0, quotient);
    ary->set(state, 1, mod);
    return ary;
  }

  INTEGER Bignum::mod(STATE, FIXNUM denominator) {
    INTEGER mod = Object::i2n(0);
    divide(state, denominator, &mod);
    return mod;
  }
  
  INTEGER Bignum::mod(STATE, Bignum* denominator) {
    INTEGER mod = Object::i2n(0);
    divide(state, denominator, &mod);
    return mod;
  }

  bool Bignum::is_zero(STATE) {
    return mp_iszero(MP(this)) ? TRUE : FALSE;
  }

  INTEGER Bignum::bit_and(STATE, INTEGER b) {
    NMP;

    if(kind_of<Fixnum>(b)) {
      b = Bignum::create(state, b->n2i());
    }

    /* Perhaps this should use mp_and rather than our own version */
    bignum_bitwise_op(BITWISE_OP_AND, MP(this), MP(b), n);
    return Bignum::normalize(state, n_obj);
  }

  INTEGER Bignum::bit_or(STATE, INTEGER b) {
    NMP;

    if(kind_of<Fixnum>(b)) {
      b = Bignum::create(state, b->n2i());
    }
    /* Perhaps this should use mp_or rather than our own version */
    bignum_bitwise_op(BITWISE_OP_OR, MP(this), MP(b), n);
    return Bignum::normalize(state, n_obj);
  }

  INTEGER Bignum::bit_xor(STATE, INTEGER b) {
    NMP;

    if(kind_of<Fixnum>(b)) {
      b = Bignum::create(state, b->n2i());
    }
    /* Perhaps this should use mp_xor rather than our own version */
    bignum_bitwise_op(BITWISE_OP_XOR, MP(this), MP(b), n);
    return Bignum::normalize(state, n_obj);
  }

  INTEGER Bignum::invert(STATE) {
    NMP;

    mp_int a; mp_init(&a);
    mp_int b; mp_init_set_int(&b, 1);

    /* inversion by -(a)-1 */
    mp_neg(MP(this), &a);
    mp_sub(&a, &b, n);

    mp_clear(&a); mp_clear(&b);
    return Bignum::normalize(state, n_obj);
  }

  INTEGER Bignum::neg(STATE) {
    NMP;

    mp_neg(MP(this), n);
    return Bignum::normalize(state, n_obj);
  }

  /* These 2 don't use mp_lshd because it shifts by internal digits,
     not bits. */

  INTEGER Bignum::left_shift(STATE, INTEGER bits) {
    NMP;
    int shift = bits->n2i();
    mp_int *a = MP(this);

    mp_mul_2d(a, shift, n);
    n->sign = a->sign;
    return Bignum::normalize(state, n_obj);
  }

  INTEGER Bignum::right_shift(STATE, INTEGER bits) {
    NMP;
    int shift = bits->n2i();
    mp_int * a = MP(this);

    if ((shift / DIGIT_BIT) >= a->used) {
      if (a->sign == MP_ZPOS)
        return Object::i2n(0);
      else
        return Object::i2n(-1);
    }

    if (shift == 0) {
      mp_copy(a, n);
    } else {
      int need_floor = (a->sign == MP_NEG) && (DIGIT(a, 0) & 1);

      mp_div_2d(a, shift, n, NULL);
      n->sign = a->sign;
      if (need_floor) {
        /* We sometimes have to simulate the rounding toward negative
           infinity that would happen if we were using a twos-complement
           representation.  We know we'll never overflow or need to grow,
           but we may need to back away from zero.  (libtommath doesn't
           seem to have an increment-in-place function.) */
        long i = 0;
        BDIGIT_DBL num = 1;

        if (n->used == 0)
          n->used = 1;
        while (i < n->used) {
          num += DIGIT(n,i);
          DIGIT(n,i++) = num & (DIGIT_RADIX-1);
          num = num >> DIGIT_BIT;
        }
      }
    }

    return Bignum::normalize(state, n_obj);
  }

  OBJECT Bignum::equal(STATE, INTEGER b) {
    if(kind_of<Fixnum>(b)) {
      b = Bignum::create(state, b->n2i());
    }

    if(mp_cmp(MP(this), MP(b)) == MP_EQ) {
      return Qtrue;
    }
    return Qfalse;
  }

  FIXNUM Bignum::compare(STATE, INTEGER b) {
    if(kind_of<Fixnum>(b)) {
      b = Bignum::create(state, b->n2i());
    }

    switch(mp_cmp(MP(this), MP(b))) {
      case MP_LT:
        return Object::i2n(-1);
      case MP_GT:
        return Object::i2n(1);
    }
    return Object::i2n(0);
  }

  OBJECT Bignum::gt(STATE, INTEGER b) {
    if(kind_of<Fixnum>(b)) {
      b = Bignum::create(state, b->n2i());
    }

    if(mp_cmp(MP(this), MP(b)) == MP_GT) {
      return Qtrue;
    }
    return Qfalse;
  }

  OBJECT Bignum::ge(STATE, INTEGER b) {
    int r;

    if(kind_of<Fixnum>(b)) {
      b = Bignum::create(state, b->n2i());
    }

    r = mp_cmp(MP(this), MP(b));
    if(r == MP_GT || r == MP_EQ) {
      return Qtrue;
    }
    return Qfalse;
  }

  OBJECT Bignum::lt(STATE, INTEGER b) {
    if(kind_of<Fixnum>(b)) {
      b = Bignum::create(state, b->n2i());
    }

    if(mp_cmp(MP(this), MP(b)) == MP_LT) {
      return Qtrue;
    }
    return Qfalse;
  }

  OBJECT Bignum::le(STATE, INTEGER b) {
    int r;

    if(kind_of<Fixnum>(b)) {
      b = Bignum::create(state, b->n2i());
    }

    r = mp_cmp(MP(this), MP(b));
    if(r == MP_LT || r == MP_EQ) {
      return Qtrue;
    }
    return Qfalse;
  }

  int Bignum::to_int(STATE) {
    if(MP(this)->sign == MP_NEG) {
      return -mp_get_int(MP(this));
    }
    return mp_get_int(MP(this));
  }

  native_int Bignum::to_nint() {
    if(MP(this)->sign == MP_NEG) {
      return -mp_get_int(MP(this));
    }
    return mp_get_int(MP(this));
  }

  int Bignum::to_i(STATE) {
    if(MP(this)->sign == MP_NEG) {
      return -mp_get_int(MP(this));
    }
    return mp_get_int(MP(this));
  }

  unsigned int Bignum::to_ui(STATE) {
    return (unsigned int)mp_get_int(MP(this));
  }

  unsigned long long Bignum::to_ull(STATE) {
    mp_int t;
    mp_int *s = MP(this);
    unsigned long long out, tmp;

    /* mp_get_int() gets only the lower 32 bits, on any platform. */
    out = mp_get_int(s);

    mp_init(&t);
    mp_div_2d(s, 32, &t, NULL);

    tmp = mp_get_int(&t);
    out |= tmp << 32;

    mp_clear(&t);
    return out;
  }

  long long Bignum::to_ll(STATE) {
    mp_int *s = MP(this);

    if (s->sign == MP_NEG)
      return -to_ull(state);
    else
      return to_ull(state);
  }

  Bignum* Bignum::from_ull(STATE, unsigned long long val) {
    mp_int low, high;

    mp_init_set_int(&low, val & 0xffffffff);
    mp_init_set_int(&high, val >> 32);
    mp_mul_2d(&high, 32, &high);

    Bignum* ret = Bignum::new_unsigned(state, 0);

    mp_or(&low, &high, MP(ret));

    mp_clear(&low);
    mp_clear(&high);

    return ret;
  }

  Bignum* Bignum::from_ll(STATE, long long val) {
    Bignum* ret;

    if(val < 0) {
      ret = Bignum::from_ull(state, (unsigned long long)-val);
      MP(ret)->sign = MP_NEG;
    } else {
      ret = Bignum::from_ull(state, (unsigned long long)val);
    }

    return ret;
  }

  String* Bignum::to_s(STATE, INTEGER radix) {
    char *buf;
    int sz = 1024;
    int k;
    String* obj;

    for(;;) {
      buf = ALLOC_N(char, sz);
      mp_toradix_nd(MP(this), buf, radix->n2i(), sz, &k);
      if(k < sz - 2) {
        obj = String::create(state, buf);
        FREE(buf);
        return obj;
      }
      FREE(buf);
      sz += 1024;
    }
  }

  OBJECT Bignum::from_string_detect(STATE, const char *str) {
    const char *s;
    int radix;
    int sign;
    NMP;
    s = str;
    sign = 1;
    while(isspace(*s)) { s++; }
    if(*s == '+') {
      s++;
    } else if(*s == '-') {
      sign = 0;
      s++;
    }
    radix = 10;
    if(*s == '0') {
      switch(s[1]) {
        case 'x': case 'X':
          radix = 16; s += 2;
          break;
        case 'b': case 'B':
          radix = 2; s += 2;
          break;
        case 'o': case 'O':
          radix = 8; s += 2;
          break;
        case 'd': case 'D':
          radix = 10; s += 2;
          break;
        default:
          radix = 8; s += 1;
      }
    }
    mp_read_radix(n, s, radix);

    if(!sign) {
      n->sign = MP_NEG;
    }

    return Bignum::normalize(state, n_obj);
  }

  OBJECT Bignum::from_string(STATE, const char *str, size_t radix) {
    NMP;
    mp_read_radix(n, str, radix);
    return Bignum::normalize(state, n_obj);
  }

  void Bignum::into_string(STATE, size_t radix, char *buf, size_t sz) {
    int k;
    mp_toradix_nd(MP(this), buf, radix, sz, &k);
  }

  double Bignum::to_double(STATE) {
    int i;
    double res;
    double m;
    mp_int *a;

    a = MP(this);

    if (a->used == 0) {
      return 0;
    }

    /* get number of digits of the lsb we have to read */
    i = a->used;
    m = DIGIT_RADIX;

    /* get most significant digit of result */
    res = DIGIT(a,i);

    while (--i >= 0) {
      res = (res * m) + DIGIT(a,i);
    }

    if(isinf(res)) {
      /* Bignum out of range */
      res = HUGE_VAL;
    }

    if(a->sign == MP_NEG) res = -res;

    return res;
  }

  OBJECT Bignum::from_double(STATE, double d) {
    NMP;

    long i = 0;
    BDIGIT_DBL c;
    double value;

    value = (d < 0) ? -d : d;

    /*
       if (isinf(d)) {
       rb_raise(rb_eFloatDomainError, d < 0 ? "-Infinity" : "Infinity");
       }
       if (isnan(d)) {
       rb_raise(rb_eFloatDomainError, "NaN");
       }
       */

    while (!(value <= (LONG_MAX >> 1)) || 0 != (long)value) {
      value = value / (double)(DIGIT_RADIX);
      i++;
    }

    mp_grow(n, i);

    while (i--) {
      value *= DIGIT_RADIX;
      c = (BDIGIT_DBL) value;
      value -= c;
      DIGIT(n,i) = c;
      n->used += 1;
    }

    if (d < 0) {
      mp_neg(n, n);
    }

    return Bignum::normalize(state, n_obj);
  }

  OBJECT Bignum::size(STATE)
  {
    int bits = mp_count_bits(MP(this));
    int bytes = (bits + 7) / 8;

    /* MRI returns this in words, but thats an implementation detail as far
       as I'm concerned. */
    return Object::i2n(bytes);
  }

  hashval Bignum::hash_bignum(STATE)
  {
    mp_int *a = MP(this);

    /* Apparently, a couple bits of each a->dp[n] aren't actually used,
       (e.g. when DIGIT_BIT is 60) so this hash is actually including
       that unused memory.  This might only be a problem if calculations
       are leaving cruft in those unused bits.  However, since Bignums
       are immutable, this shouldn't happen to us. */
    return String::hash_str((unsigned char *)a->dp, a->used * sizeof(mp_digit));
  }

}
