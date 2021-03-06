#ifndef RBX_BUILTIN_NATIVEMETHOD_HPP
#define RBX_BUILTIN_NATIVEMETHOD_HPP

/* Project */
#include "vm/executor.hpp"
#include "vm/vm.hpp"

#include "builtin/class.hpp"
#include "builtin/executable.hpp"
#include "builtin/string.hpp"
#include "builtin/symbol.hpp"

#include "util/thread.hpp"
#include "gc/root.hpp"

#include "vm/object_utils.hpp"

#include "capi/tag.hpp"
#include "capi/value.hpp"
#include "capi/handle.hpp"

namespace rubinius {
  class ExceptionPoint;
  class Message;
  class NativeMethodFrame;

  /** Tracks RARRAY and RSTRING structs */
  typedef std::tr1::unordered_map<capi::Handle*, void*> CApiStructs;

  /**
   * Thread-local info about native method calls. @see NativeMethodFrame.
   */
  class NativeMethodEnvironment {
    /** VM in which executing. */
    VM*                 state_;
    /** Current callframe in Ruby-land. */
    CallFrame*          current_call_frame_;
    /** Current native callframe. */
    NativeMethodFrame*  current_native_frame_;
    ExceptionPoint*     current_ep_;

  public:   /* Class Interface */
    NativeMethodEnvironment(STATE)
      : state_(state)
      , current_call_frame_(0)
      , current_native_frame_(0)
      , current_ep_(0)
    {}

    /** Obtain the NativeMethodEnvironment for this thread. */
    static NativeMethodEnvironment* get();

  public:   /* Interface methods */

    /** Create or retrieve VALUE for obj. */
    VALUE get_handle(Object* obj);

    /** Delete a global Object and its VALUE. */
    void delete_global(VALUE handle);

    /** GC marking for Objects behind VALUEs. */
    void mark_handles(ObjectMark& mark);

  public:
    /** Obtain the Object the VALUE represents. */
    Object* get_object(VALUE val) {
      if(CAPI_REFERENCE_P(val)) {
        capi::Handle* handle = capi::Handle::from(val);
        if(!handle->valid_p()) {
          handle->debug_print();
          rubinius::abort();
        }

        return handle->object();
      } else if(FIXNUM_P(val) || SYMBOL_P(val)) {
        return reinterpret_cast<Object*>(val);
      } else if(CAPI_FALSE_P(val)) {
        return Qfalse;
      } else if(CAPI_TRUE_P(val)) {
        return Qtrue;
      } else if(CAPI_NIL_P(val)) {
        return Qnil;
      } else if(CAPI_UNDEF_P(val)) {
        return Qundef;
      }

      rubinius::bug("requested Object for unknown NativeMethod handle type");
      return Qnil; // keep compiler happy
    }


  public:   /* Accessors */

    Object* block();

    void set_state(VM* vm) {
      state_ = vm;
    }

    VM* state() {
      return state_;
    }

    CallFrame* current_call_frame() {
      return current_call_frame_;
    }

    void set_current_call_frame(CallFrame* frame) {
      current_call_frame_ = frame;
    }

    NativeMethodFrame* current_native_frame() {
      return current_native_frame_;
    }

    void set_current_native_frame(NativeMethodFrame* frame) {
      current_native_frame_ = frame;
    }

    ExceptionPoint* current_ep() {
      return current_ep_;
    }

    void set_current_ep(ExceptionPoint* ep) {
      current_ep_ = ep;
    }

    /** Set of Handles available in current Frame (convenience.) */
    capi::HandleSet& handles();

    /** Flush RARRAY, RSTRING, etc. caches, possibly releasing memory. */
    void flush_cached_data();

    /** Updates cached data with changes to the Ruby objects. */
    void update_cached_data();
  };


  /**
   *  Call frame for a native method. @see NativeMethodEnvironment.
   */
  class NativeMethodFrame {
    /** Native Frame active before this call. @note This may NOT be the sender. --rue */
    NativeMethodFrame* previous_;
    /** HandleSet to Objects used in this Frame. */
    capi::HandleSet handles_;

    /** Handle for the block passed in **/
    VALUE block_;

    /** Handle for receiver **/
    VALUE receiver_;

    /** Handle for module method came from **/
    VALUE module_;

    /** Handle for method being invoked **/
    VALUE method_;

  public:
    NativeMethodFrame(NativeMethodFrame* prev)
      : previous_(prev)
      , block_(cCApiHandleQnil)
    {}

    ~NativeMethodFrame();

  public:     /* Interface methods */

    /** Currently active/used Frame in this Environment i.e. thread. */
    static NativeMethodFrame* current() {
      return NativeMethodEnvironment::get()->current_native_frame();
    }

    /** Create or retrieve a VALUE for the Object. */
    VALUE get_handle(VM*, Object* obj);

    /** Obtain the Object the VALUE represents. */
    Object* get_object(VALUE hndl);

    /** Flush RARRAY, RSTRING, etc. caches, possibly releasing memory. */
    void flush_cached_data();

    /** Updates cached data with changes to the Ruby objects. */
    void update_cached_data();

  public:     /* Accessors */

    /** HandleSet to Objects used in this Frame. */
    capi::HandleSet& handles() {
      return handles_;
    }

    /** Native Frame active before this call. */
    NativeMethodFrame* previous() {
      return previous_;
    }

    void setup(VALUE recv, VALUE block, VALUE method, VALUE module) {
      receiver_ = recv;
      method_ = method;
      block_ = block;
      module_ = module;
    }

    VALUE block() {
      return block_;
    }

    VALUE receiver() {
      return receiver_;
    }

    VALUE method() {
      return method_;
    }

    VALUE module() {
      return module_;
    }

  };


  /**
   *  The various special method arities from a C method. If not one of these,
   *  then the number denotes the exact number of args in addition to the
   *  receiver instead.
   */
  enum Arity {
    INIT_FUNCTION = -99,
    ARGS_IN_RUBY_ARRAY = -3,
    RECEIVER_PLUS_ARGS_IN_RUBY_ARRAY = -2,
    ARG_COUNT_ARGS_IN_C_ARRAY_PLUS_RECEIVER = -1
  };


  /* The various function signatures needed since C++ requires strict typing here. */

  /** Generic function pointer used to store any type of functor. */
  typedef void (*GenericFunctor)(void);

  /* Actual functor types. */

  typedef void (*InitFunctor)      (void);   /**< The Init_<name> function. */

  typedef VALUE (*ArgcFunctor)     (int, VALUE*, VALUE);
  typedef VALUE (*OneArgFunctor)   (VALUE);
  typedef VALUE (*TwoArgFunctor)   (VALUE, VALUE);
  typedef VALUE (*ThreeArgFunctor) (VALUE, VALUE, VALUE);
  typedef VALUE (*FourArgFunctor)  (VALUE, VALUE, VALUE, VALUE);
  typedef VALUE (*FiveArgFunctor)  (VALUE, VALUE, VALUE, VALUE, VALUE);
  typedef VALUE (*SixArgFunctor)   (VALUE, VALUE, VALUE, VALUE, VALUE, VALUE);
  typedef VALUE (*SevenArgFunctor) (VALUE, VALUE, VALUE, VALUE, VALUE, VALUE, VALUE);
  typedef VALUE (*EightArgFunctor) (VALUE, VALUE, VALUE, VALUE, VALUE, VALUE, VALUE, VALUE);
  typedef VALUE (*NineArgFunctor)  (VALUE, VALUE, VALUE, VALUE, VALUE, VALUE, VALUE, VALUE,
                                    VALUE);
  typedef VALUE (*TenArgFunctor)   (VALUE, VALUE, VALUE, VALUE, VALUE, VALUE, VALUE, VALUE,
                                    VALUE, VALUE);


  /**
   *  Ruby side of a NativeMethod, i.e. a C-implemented method.
   *
   *  The possible signatures for the C function are:
   *
   *    VALUE func(VALUE argument_array);
   *    VALUE func(VALUE receiver, VALUE argument_array);
   *    VALUE func(VALUE receiver, int argument_count, VALUE*);
   *    VALUE func(VALUE receiver, ...);    // Some limited number of VALUE arguments
   *
   *  VALUE is an opaque type from which a handle to the actual object
   *  can be extracted. User code never operates on the VALUE itself.
   *
   *  @todo   Add tests specifically for NativeMethod.
   *          Currently only in Subtend tests.
   */
  class NativeMethod : public Executable {
    /** Arity of the method. @see Arity. */
    Fixnum* arity_;                                   // slot
    /** C file in which rb_define_method called. */
    String* file_;                                    // slot
    /** Name given at creation time. */
    Symbol* name_;                                    // slot
    /** Module on which created. */
    Module* module_;                                  // slot

    /** Function object that implements this method. */
    void* functor_;

  public:   /* Accessors */

    /** Arity of the method within. @see Arity. */
    attr_accessor(arity, Fixnum);
    /** C file in which rb_define_method called. */
    attr_accessor(file, String);
    /** Name given at creation time. */
    attr_accessor(name, Symbol);
    /** Module on which created. */
    attr_accessor(module, Module);

  public:   /* Ruby bookkeeping */

    /** Statically held object type. */
    const static object_type type = NativeMethodType;

    /** Set class up in the VM. @see vm/ontology.cpp. */
    static void init(VM* state);

    // Called when starting a new native thread, initializes any thread
    // local data.
    static void init_thread(VM* state);

    // Called when a thread is exitting, to cleanup the thread local data.
    static void cleanup_thread(VM* state);


  public:   /* Ctors */

    /**
     *  Create a NativeMethod object.
     *
     *  May take one of several types of functors but the calling
     *  code is responsible for figuring out which type to cast
     *  the functor back to for use, that information is not
     *  stored here.
     *
     *  @see  functor_as() for the cast back.
     */
    template <typename FunctorType>
      static NativeMethod* create(VM* state,
                                  String* file_name = as<String>(Qnil),
                                  Module* module = as<Module>(Qnil),
                                  Symbol* method_name = as<Symbol>(Qnil),
                                  FunctorType functor = static_cast<GenericFunctor>(NULL),
                                  Fixnum* arity = as<Fixnum>(Qnil))
      {
        NativeMethod* nmethod = state->new_object<NativeMethod>(G(nmethod));

        nmethod->arity(state, arity);
        nmethod->file(state, file_name);
        nmethod->name(state, method_name);
        nmethod->module(state, module);

        nmethod->functor_ = reinterpret_cast<void*>(functor);

        nmethod->set_executor(&NativeMethod::executor_implementation);

        nmethod->primitive(state, state->symbol("nativemethod_call"));
        nmethod->serial(state, Fixnum::from(0));

        return nmethod;
      }

    /** Allocate a functional but empty NativeMethod. */
    static NativeMethod* allocate(VM* state);


  public:   /* Class Interface */

    /** Set up and call native method. */
    static Object* executor_implementation(STATE, CallFrame* call_frame, Dispatch& msg,
                                           Arguments& message);

    /**
     *  Attempt to load a C extension library and its main function.
     *
     *  The path should be the full path (relative or absolute) to the
     *  library, without the file extension. The name should be the
     *  entry point name, i.e. "Init_<extension>", where extension is
     *  the basename of the library. The function signature is:
     *
     *    void Init_<name>();
     *
     *  A NativeMethod for the function is returned, and can be executed
     *  to actually perform the loading.
     *
     *  Of possibly minor interest, the method's module is set to
     *  Rubinius. It should be updated accordingly when properly
     *  entered.
     */
    // Ruby.primitive :nativemethod_load_extension_entry_point
    static NativeMethod* load_extension_entry_point(STATE, String* path, String* name);


  public:   /* Instance methods */

    /** Call the C function. */
    Object* call(STATE, NativeMethodEnvironment* env, Arguments& msg);


    /** Return the functor cast into the specified type. */
    template <typename FunctorType>
      FunctorType functor_as() const
      {
        return reinterpret_cast<FunctorType>(functor_);
      }

  public:   /* Type information */

    /** Type information for NativeMethod. */
    class Info : public Executable::Info {
    public:
      BASIC_TYPEINFO(Executable::Info)
    };

  };    /* NativeMethod */

}

#endif  /* NATIVEMETHOD_HPP */
