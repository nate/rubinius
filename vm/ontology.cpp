#include <errno.h>

#include "objects.hpp"
#include "objectmemory.hpp"
#include "vm.hpp"

#include "builtin/access_variable.hpp"
#include "builtin/array.hpp"
#include "builtin/block_environment.hpp"
#include "builtin/bytearray.hpp"
#include "builtin/class.hpp"
#include "builtin/compactlookuptable.hpp"
#include "builtin/compiledmethod.hpp"
#include "builtin/contexts.hpp"
#include "builtin/dir.hpp"
#include "builtin/executable.hpp"
#include "builtin/fixnum.hpp"
#include "builtin/float.hpp"
#include "builtin/io.hpp"
#include "builtin/iseq.hpp"
#include "builtin/list.hpp"
#include "builtin/lookuptable.hpp"
#include "builtin/methodtable.hpp"
#include "builtin/regexp.hpp"
#include "builtin/selector.hpp"
#include "builtin/sendsite.hpp"
#include "builtin/staticscope.hpp"
#include "builtin/string.hpp"
#include "builtin/symbol.hpp"
#include "builtin/task.hpp"
#include "builtin/thread.hpp"
#include "builtin/time.hpp"
#include "builtin/tuple.hpp"

#define SPECIAL_CLASS_MASK 0x1f
#define SPECIAL_CLASS_SIZE 32
#define CUSTOM_CLASS GO(object)

namespace rubinius {

  /* State is a VM* so, we can just use this in here */
  #define state this

  // Reset macros since we're inside state
#undef G
#undef GO
#define G(whatever) globals.whatever.get()
#define GO(whatever) globals.whatever

  void VM::bootstrap_class() {
    /* Class is created first by hand, and twittle to setup the internal
       recursion. */
    Class *cls = (Class*)om->allocate_object(Class::fields);

    /* We create these 7 classes in a particular way and in a particular
     * order. We need all 7 to create fully initialized Classes and
     * Modules, so we just create them all uninitialized, then initialize
     * them all at once */

    /* Class's klass is Class */
    cls->klass = cls;
    cls->obj_type = ClassType;

    cls->instance_fields = Fixnum::from(Class::fields);
    cls->has_ivars = Qtrue;
    cls->set_object_type(ClassType);

    /* Set Class into the globals */
    GO(klass).set(cls);

    /* Now do Object */
    Class *object = new_basic_class((Class*)Qnil, Object::fields);
    GO(object).set(object);

    /* Now Module */
    GO(module).set(new_basic_class(object, Module::fields));
    G(module)->set_object_type(ModuleType);

    /* Fixup Class's superclass to be Module */
    cls->superclass = G(module);

    /* Create MetaClass */
    GO(metaclass).set(new_basic_class(cls, MetaClass::fields));
    G(metaclass)->set_object_type(MetaclassType);

    /* Create Tuple */
    GO(tuple).set(new_basic_class(object, Tuple::fields));
    G(tuple)->set_object_type(TupleType);

    /* Create LookupTable */
    GO(lookuptable).set(new_basic_class(object, LookupTable::fields));
    G(lookuptable)->set_object_type(LookupTableType);

    /* Create MethodTable */
    GO(methtbl).set(new_basic_class(G(lookuptable), MethodTable::fields));
    G(methtbl)->set_object_type(MTType);

    /* Now, we have:
     *  Class
     *  Module
     *  MetaClass
     *  Object
     *  Tuple
     *  LookupTable
     *  MethodTable
     *
     *  With these 7 in place, we can now create fully initialized classes
     *  and modules. */

    /* Hook up the MetaClass protocols.
     * MetaClasses of subclasses point to the MetaClass of the
     * superclass. */
    OBJECT mc = MetaClass::attach(this, object, cls);
    mc = MetaClass::attach(this, G(module), mc);
    MetaClass::attach(this, cls, mc);

    // TODO not sure these are being setup properly
    MetaClass::attach(this, G(metaclass));
    MetaClass::attach(this, G(tuple));
    MetaClass::attach(this, G(lookuptable));
    MetaClass::attach(this, G(methtbl));

    // Now, finish initializing the special 7
    G(object)->setup(this, "Object");
    G(klass)->setup(this, "Class");
    G(module)->setup(this, "Module");
    G(metaclass)->setup(this, "MetaClass");
    G(tuple)->setup(this, "Tuple");
    G(lookuptable)->setup(this, "LookupTable");
    G(methtbl)->setup(this, "MethodTable");
  }

  void VM::initialize_builtin_classes() {
    // Create the immediate classes.
    GO(nil_class).set(new_class("NilClass"));
    GO(true_class).set(new_class("TrueClass"));
    GO(false_class).set(new_class("FalseClass"));

    Class* numeric = new_class("Numeric");
    GO(numeric).set(numeric);
    Class* integer = new_class("Integer", numeric);
    GO(integer).set(integer);
    GO(fixnum_class).set(new_class("Fixnum", integer));
    G(fixnum_class)->instance_type = Fixnum::from(FixnumType);
    Symbol::init(this);

    // Setup the special_class lookup table. We use this to resolve
    // the classes for Fixnum's, nil, true and false.
    for(size_t i = 0; i < SPECIAL_CLASS_SIZE; i += 4) {
      globals.special_classes[i + 0] = GO(object); /* unused slot */
      globals.special_classes[i + 1] = GO(fixnum_class);
      globals.special_classes[i + 2] = GO(object); /* unused slot */
      if(((i + 3) & 0x7) == 0x3) {
        globals.special_classes[i + 3] = GO(symbol);
      } else {
        globals.special_classes[i + 3] = CUSTOM_CLASS;
      }
    }

    globals.special_classes[(uintptr_t)Qundef] = GO(object); /* unused slot */
    globals.special_classes[(uintptr_t)Qfalse] = GO(false_class);
    globals.special_classes[(uintptr_t)Qnil  ] = GO(nil_class);
    globals.special_classes[(uintptr_t)Qtrue ] = GO(true_class);


    // Let all the builtin classes initialize themselves. This
    // typically means creating a Ruby class.
    Array::init(this);
    ByteArray::init(this);
    String::init(this);
    Executable::init(this);
    CompiledMethod::init(this);
    IO::init(this);
    BlockEnvironment::init(this);
    StaticScope::init(this);
    Dir::init(this);
    CompactLookupTable::init(this);
    Time::init(this);
    Regexp::init(this);
    Bignum::init(this);
    Float::init(this);
    MethodContext::init(this);
    InstructionSequence::init(this);
    List::init(this);
    SendSite::init(this);
    Selector::init(this);
    init_ffi();
    Task::init(this);
    Thread::init(this);
    AccessVariable::init(this);
  }

  // TODO: document all the sections of bootstrap_ontology
  /* Creates the rubinius object universe from scratch. */
  void VM::bootstrap_ontology() {

    /*
     * Bootstrap everything so we can create fully initialized
     * Classes.
     */
    bootstrap_class();

    /*
     * Everything is now setup for us to make fully initialized
     * classes.
     */

    bootstrap_symbol();
    initialize_builtin_classes();
    bootstrap_exceptions();

    /*
     * Create any 'stock' objects
     */

    Object* main = om->new_object(G(object), 1);
    GO(main).set(main);
    G(object)->set_const(this, "MAIN", main); // HACK test hooking up MAIN

    /*
     * Create our Rubinius module that we hang stuff off
     */

    Module* rbx = new_module("Rubinius");
    GO(rubinius).set(rbx);
    GO(vm).set(new_class_under("VM", rbx));

    /*
     * Setup the table we use to store ivars for immediates
     */

    GO(external_ivars).set(LookupTable::create(this));

    initialize_platform_data();
  }

  void VM::initialize_platform_data() {
    // HACK test hooking up IO
    IO* in_io  = IO::create(state, fileno(stdin));
    IO* out_io = IO::create(state, fileno(stdout));
    IO* err_io = IO::create(state, fileno(stderr));

    G(object)->set_const(state, "STDIN",  in_io);
    G(object)->set_const(state, "STDOUT", out_io);
    G(object)->set_const(state, "STDERR", err_io);

    if(sizeof(int) == sizeof(long)) {
      G(rubinius)->set_const(state, "L64", Qfalse);
    } else {
      G(rubinius)->set_const(state, "L64", Qtrue);
    }
  }

  void VM::bootstrap_symbol() {
#define add_sym(name) GO(sym_ ## name).set(symbol(#name))
    add_sym(object_id);
    add_sym(method_missing);
    add_sym(inherited);
    add_sym(opened_class);
    add_sym(from_literal);
    add_sym(method_added);
    add_sym(send);
    add_sym(public);
    add_sym(private);
    add_sym(protected);
    add_sym(const_missing);
    add_sym(object_id);
    add_sym(call);
#undef add_sym
    GO(sym_s_method_added).set(symbol("singleton_method_added"));
    GO(sym_init_copy).set(symbol("initialize_copy"));
    GO(sym_plus).set(symbol("+"));
    GO(sym_minus).set(symbol("-"));
    GO(sym_equal).set(symbol("=="));
    GO(sym_nequal).set(symbol("!="));
    GO(sym_tequal).set(symbol("==="));
    GO(sym_lt).set(symbol("<"));
    GO(sym_gt).set(symbol(">"));
  }

  void VM::bootstrap_exceptions() {
    int sz;
    sz = 3;

    Class *exc, *scp, *std, *arg, *nam, *loe, *rex, *stk, *sxp, *sce, *type, *lje, *vm;
    Class* fce;

#define dexc(name, sup) new_class(#name, sup, sz)

    exc = dexc(Exception, G(object));
    exc->set_object_type(ExceptionType);
    GO(exception).set(exc);
    dexc(fatal, exc);
    vm = dexc(VMError, exc);
    dexc(VMAssertion, vm);
    scp = dexc(ScriptError, exc);
    std = dexc(StandardError, exc);
    type = dexc(TypeError, std);
    arg = dexc(ArgumentError, std);
    nam = dexc(NameError, std);
    rex = dexc(RegexpError, std);
    dexc(NoMethodError, nam);
    dexc(SyntaxError, scp);
    loe = dexc(LoadError, scp);
    dexc(RuntimeError, std);
    sce = dexc(SystemCallError, std);
    stk = dexc(StackError, exc);
    sxp = dexc(StackExploded, stk);

    lje = dexc(LocalJumpError, std);
    dexc(IllegalLongReturn, lje);

    fce = dexc(FlowControlException, exc);
    dexc(ReturnException, fce);
    dexc(LongReturnException, fce);

    GO(exc_type).set(type);
    GO(exc_arg).set(arg);
    GO(exc_loe).set(loe);
    GO(exc_rex).set(rex);

    GO(exc_stack_explosion).set(sxp);
    GO(exc_primitive_failure).set(dexc(PrimitiveFailure, exc));

    GO(exc_segfault).set(dexc(MemorySegmentionError, exc));

    Module* ern = new_module("Errno");

    GO(errno_mapping).set(LookupTable::create(state));

    ern->set_const(state, symbol("Mapping"), G(errno_mapping));

    sz = 4;

#define set_syserr(num, name) ({ \
    Class* _cls = new_class(name, sce, sz, ern); \
    _cls->set_const(state, symbol("Errno"), Fixnum::from(num)); \
    G(errno_mapping)->store(state, Fixnum::from(num), _cls); \
    })

    /*
     * Stolen from MRI
     */

#ifdef EPERM
    set_syserr(EPERM, "EPERM");
#endif
#ifdef ENOENT
    set_syserr(ENOENT, "ENOENT");
#endif
#ifdef ESRCH
    set_syserr(ESRCH, "ESRCH");
#endif
#ifdef EINTR
    set_syserr(EINTR, "EINTR");
#endif
#ifdef EIO
    set_syserr(EIO, "EIO");
#endif
#ifdef ENXIO
    set_syserr(ENXIO, "ENXIO");
#endif
#ifdef E2BIG
    set_syserr(E2BIG, "E2BIG");
#endif
#ifdef ENOEXEC
    set_syserr(ENOEXEC, "ENOEXEC");
#endif
#ifdef EBADF
    set_syserr(EBADF, "EBADF");
#endif
#ifdef ECHILD
    set_syserr(ECHILD, "ECHILD");
#endif
#ifdef EAGAIN
    set_syserr(EAGAIN, "EAGAIN");
#endif
#ifdef ENOMEM
    set_syserr(ENOMEM, "ENOMEM");
#endif
#ifdef EACCES
    set_syserr(EACCES, "EACCES");
#endif
#ifdef EFAULT
    set_syserr(EFAULT, "EFAULT");
#endif
#ifdef ENOTBLK
    set_syserr(ENOTBLK, "ENOTBLK");
#endif
#ifdef EBUSY
    set_syserr(EBUSY, "EBUSY");
#endif
#ifdef EEXIST
    set_syserr(EEXIST, "EEXIST");
#endif
#ifdef EXDEV
    set_syserr(EXDEV, "EXDEV");
#endif
#ifdef ENODEV
    set_syserr(ENODEV, "ENODEV");
#endif
#ifdef ENOTDIR
    set_syserr(ENOTDIR, "ENOTDIR");
#endif
#ifdef EISDIR
    set_syserr(EISDIR, "EISDIR");
#endif
#ifdef EINVAL
    set_syserr(EINVAL, "EINVAL");
#endif
#ifdef ENFILE
    set_syserr(ENFILE, "ENFILE");
#endif
#ifdef EMFILE
    set_syserr(EMFILE, "EMFILE");
#endif
#ifdef ENOTTY
    set_syserr(ENOTTY, "ENOTTY");
#endif
#ifdef ETXTBSY
    set_syserr(ETXTBSY, "ETXTBSY");
#endif
#ifdef EFBIG
    set_syserr(EFBIG, "EFBIG");
#endif
#ifdef ENOSPC
    set_syserr(ENOSPC, "ENOSPC");
#endif
#ifdef ESPIPE
    set_syserr(ESPIPE, "ESPIPE");
#endif
#ifdef EROFS
    set_syserr(EROFS, "EROFS");
#endif
#ifdef EMLINK
    set_syserr(EMLINK, "EMLINK");
#endif
#ifdef EPIPE
    set_syserr(EPIPE, "EPIPE");
#endif
#ifdef EDOM
    set_syserr(EDOM, "EDOM");
#endif
#ifdef ERANGE
    set_syserr(ERANGE, "ERANGE");
#endif
#ifdef EDEADLK
    set_syserr(EDEADLK, "EDEADLK");
#endif
#ifdef ENAMETOOLONG
    set_syserr(ENAMETOOLONG, "ENAMETOOLONG");
#endif
#ifdef ENOLCK
    set_syserr(ENOLCK, "ENOLCK");
#endif
#ifdef ENOSYS
    set_syserr(ENOSYS, "ENOSYS");
#endif
#ifdef ENOTEMPTY
    set_syserr(ENOTEMPTY, "ENOTEMPTY");
#endif
#ifdef ELOOP
    set_syserr(ELOOP, "ELOOP");
#endif
#ifdef EWOULDBLOCK
    set_syserr(EWOULDBLOCK, "EWOULDBLOCK");
#endif
#ifdef ENOMSG
    set_syserr(ENOMSG, "ENOMSG");
#endif
#ifdef EIDRM
    set_syserr(EIDRM, "EIDRM");
#endif
#ifdef ECHRNG
    set_syserr(ECHRNG, "ECHRNG");
#endif
#ifdef EL2NSYNC
    set_syserr(EL2NSYNC, "EL2NSYNC");
#endif
#ifdef EL3HLT
    set_syserr(EL3HLT, "EL3HLT");
#endif
#ifdef EL3RST
    set_syserr(EL3RST, "EL3RST");
#endif
#ifdef ELNRNG
    set_syserr(ELNRNG, "ELNRNG");
#endif
#ifdef EUNATCH
    set_syserr(EUNATCH, "EUNATCH");
#endif
#ifdef ENOCSI
    set_syserr(ENOCSI, "ENOCSI");
#endif
#ifdef EL2HLT
    set_syserr(EL2HLT, "EL2HLT");
#endif
#ifdef EBADE
    set_syserr(EBADE, "EBADE");
#endif
#ifdef EBADR
    set_syserr(EBADR, "EBADR");
#endif
#ifdef EXFULL
    set_syserr(EXFULL, "EXFULL");
#endif
#ifdef ENOANO
    set_syserr(ENOANO, "ENOANO");
#endif
#ifdef EBADRQC
    set_syserr(EBADRQC, "EBADRQC");
#endif
#ifdef EBADSLT
    set_syserr(EBADSLT, "EBADSLT");
#endif
#ifdef EDEADLOCK
    set_syserr(EDEADLOCK, "EDEADLOCK");
#endif
#ifdef EBFONT
    set_syserr(EBFONT, "EBFONT");
#endif
#ifdef ENOSTR
    set_syserr(ENOSTR, "ENOSTR");
#endif
#ifdef ENODATA
    set_syserr(ENODATA, "ENODATA");
#endif
#ifdef ETIME
    set_syserr(ETIME, "ETIME");
#endif
#ifdef ENOSR
    set_syserr(ENOSR, "ENOSR");
#endif
#ifdef ENONET
    set_syserr(ENONET, "ENONET");
#endif
#ifdef ENOPKG
    set_syserr(ENOPKG, "ENOPKG");
#endif
#ifdef EREMOTE
    set_syserr(EREMOTE, "EREMOTE");
#endif
#ifdef ENOLINK
    set_syserr(ENOLINK, "ENOLINK");
#endif
#ifdef EADV
    set_syserr(EADV, "EADV");
#endif
#ifdef ESRMNT
    set_syserr(ESRMNT, "ESRMNT");
#endif
#ifdef ECOMM
    set_syserr(ECOMM, "ECOMM");
#endif
#ifdef EPROTO
    set_syserr(EPROTO, "EPROTO");
#endif
#ifdef EMULTIHOP
    set_syserr(EMULTIHOP, "EMULTIHOP");
#endif
#ifdef EDOTDOT
    set_syserr(EDOTDOT, "EDOTDOT");
#endif
#ifdef EBADMSG
    set_syserr(EBADMSG, "EBADMSG");
#endif
#ifdef EOVERFLOW
    set_syserr(EOVERFLOW, "EOVERFLOW");
#endif
#ifdef ENOTUNIQ
    set_syserr(ENOTUNIQ, "ENOTUNIQ");
#endif
#ifdef EBADFD
    set_syserr(EBADFD, "EBADFD");
#endif
#ifdef EREMCHG
    set_syserr(EREMCHG, "EREMCHG");
#endif
#ifdef ELIBACC
    set_syserr(ELIBACC, "ELIBACC");
#endif
#ifdef ELIBBAD
    set_syserr(ELIBBAD, "ELIBBAD");
#endif
#ifdef ELIBSCN
    set_syserr(ELIBSCN, "ELIBSCN");
#endif
#ifdef ELIBMAX
    set_syserr(ELIBMAX, "ELIBMAX");
#endif
#ifdef ELIBEXEC
    set_syserr(ELIBEXEC, "ELIBEXEC");
#endif
#ifdef EILSEQ
    set_syserr(EILSEQ, "EILSEQ");
#endif
#ifdef ERESTART
    set_syserr(ERESTART, "ERESTART");
#endif
#ifdef ESTRPIPE
    set_syserr(ESTRPIPE, "ESTRPIPE");
#endif
#ifdef EUSERS
    set_syserr(EUSERS, "EUSERS");
#endif
#ifdef ENOTSOCK
    set_syserr(ENOTSOCK, "ENOTSOCK");
#endif
#ifdef EDESTADDRREQ
    set_syserr(EDESTADDRREQ, "EDESTADDRREQ");
#endif
#ifdef EMSGSIZE
    set_syserr(EMSGSIZE, "EMSGSIZE");
#endif
#ifdef EPROTOTYPE
    set_syserr(EPROTOTYPE, "EPROTOTYPE");
#endif
#ifdef ENOPROTOOPT
    set_syserr(ENOPROTOOPT, "ENOPROTOOPT");
#endif
#ifdef EPROTONOSUPPORT
    set_syserr(EPROTONOSUPPORT, "EPROTONOSUPPORT");
#endif
#ifdef ESOCKTNOSUPPORT
    set_syserr(ESOCKTNOSUPPORT, "ESOCKTNOSUPPORT");
#endif
#ifdef EOPNOTSUPP
    set_syserr(EOPNOTSUPP, "EOPNOTSUPP");
#endif
#ifdef EPFNOSUPPORT
    set_syserr(EPFNOSUPPORT, "EPFNOSUPPORT");
#endif
#ifdef EAFNOSUPPORT
    set_syserr(EAFNOSUPPORT, "EAFNOSUPPORT");
#endif
#ifdef EADDRINUSE
    set_syserr(EADDRINUSE, "EADDRINUSE");
#endif
#ifdef EADDRNOTAVAIL
    set_syserr(EADDRNOTAVAIL, "EADDRNOTAVAIL");
#endif
#ifdef ENETDOWN
    set_syserr(ENETDOWN, "ENETDOWN");
#endif
#ifdef ENETUNREACH
    set_syserr(ENETUNREACH, "ENETUNREACH");
#endif
#ifdef ENETRESET
    set_syserr(ENETRESET, "ENETRESET");
#endif
#ifdef ECONNABORTED
    set_syserr(ECONNABORTED, "ECONNABORTED");
#endif
#ifdef ECONNRESET
    set_syserr(ECONNRESET, "ECONNRESET");
#endif
#ifdef ENOBUFS
    set_syserr(ENOBUFS, "ENOBUFS");
#endif
#ifdef EISCONN
    set_syserr(EISCONN, "EISCONN");
#endif
#ifdef ENOTCONN
    set_syserr(ENOTCONN, "ENOTCONN");
#endif
#ifdef ESHUTDOWN
    set_syserr(ESHUTDOWN, "ESHUTDOWN");
#endif
#ifdef ETOOMANYREFS
    set_syserr(ETOOMANYREFS, "ETOOMANYREFS");
#endif
#ifdef ETIMEDOUT
    set_syserr(ETIMEDOUT, "ETIMEDOUT");
#endif
#ifdef ECONNREFUSED
    set_syserr(ECONNREFUSED, "ECONNREFUSED");
#endif
#ifdef EHOSTDOWN
    set_syserr(EHOSTDOWN, "EHOSTDOWN");
#endif
#ifdef EHOSTUNREACH
    set_syserr(EHOSTUNREACH, "EHOSTUNREACH");
#endif
#ifdef EALREADY
    set_syserr(EALREADY, "EALREADY");
#endif
#ifdef EINPROGRESS
    set_syserr(EINPROGRESS, "EINPROGRESS");
#endif
#ifdef ESTALE
    set_syserr(ESTALE, "ESTALE");
#endif
#ifdef EUCLEAN
    set_syserr(EUCLEAN, "EUCLEAN");
#endif
#ifdef ENOTNAM
    set_syserr(ENOTNAM, "ENOTNAM");
#endif
#ifdef ENAVAIL
    set_syserr(ENAVAIL, "ENAVAIL");
#endif
#ifdef EISNAM
    set_syserr(EISNAM, "EISNAM");
#endif
#ifdef EREMOTEIO
    set_syserr(EREMOTEIO, "EREMOTEIO");
#endif
#ifdef EDQUOT
    set_syserr(EDQUOT, "EDQUOT");
#endif

  }
};