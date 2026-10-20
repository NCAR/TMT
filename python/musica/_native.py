"""Typed C calls and ownership for the TMT compatibility installation."""
import ctypes as C
import os
from pathlib import Path
import sys
import numpy as np

P, Z, D, I = C.c_void_p, C.c_size_t, C.c_double, C.c_int
DP = C.POINTER(D)


class String(C.Structure):
    _fields_ = [("value", P), ("size", Z)]

    def text(self):
        return C.string_at(self.value, self.size).decode() if self.value else ""


class Error(C.Structure):
    _fields_ = [("code", I), ("severity", I), ("category", String), ("message", String)]


class Mapping(C.Structure):
    _fields_ = [("name", String), ("index", Z)]


class Mappings(C.Structure):
    _fields_ = [("items", C.POINTER(Mapping)), ("size", Z)]


def _load():
    filename = "libtmt.dylib" if sys.platform == "darwin" else "libtmt.so"
    root = Path(__file__).resolve().parents[2]
    candidates = ([Path(os.environ["TMT_LIBRARY"])] if "TMT_LIBRARY" in os.environ else [])
    candidates += [Path(__file__).parent / "lib" / filename,
                   root / "build" / filename, Path(sys.prefix) / "lib" / filename]
    candidates += [p / "lib" / filename for p in list(Path(__file__).resolve().parents)[:5]]
    for path in candidates:
        if path.is_file():
            return C.CDLL(str(path.resolve()))
    raise ImportError("TMT native library not found; set TMT_LIBRARY to its installed path")


lib = _load()


def bind(name, result, *args):
    fn = getattr(lib, name)
    fn.restype, fn.argtypes = result, list(args)
    return fn


bind("DeleteError", None, C.POINTER(Error))
bind("DeleteString", None, C.POINTER(String))
bind("DeleteMappings", None, C.POINTER(Mappings))


def call(name, *args):
    error = Error()
    result = getattr(lib, name)(*args, C.byref(error))
    try:
        if error.code:
            raise ValueError(f"{error.category.text()}: {error.message.text()}")
        return result
    finally:
        lib.DeleteError(C.byref(error))


class Handle:
    """One native owned reference. Arrays may retain it after wrapper closure."""
    def __init__(self, pointer, delete):
        if not pointer:
            raise ValueError("Native object creation returned a null handle")
        self.pointer, self.delete = pointer, getattr(lib, delete)

    def __del__(self):
        pointer = getattr(self, "pointer", None)
        if pointer:
            self.delete(pointer, None)
            self.pointer = None


class Object:
    _kind = None

    @property
    def _ptr(self):
        if getattr(self, "_handle", None) is None:
            raise ValueError("Object is closed")
        return self._handle.pointer

    @classmethod
    def _from_handle(cls, pointer):
        obj = cls.__new__(cls)
        obj._handle = Handle(pointer, "Delete" + cls._kind)
        return obj

    def close(self):
        self._handle = None

    def __enter__(self):
        self._ptr
        return self

    def __exit__(self, *_):
        self.close()


def encoded(value):
    return os.fspath(value).encode()


def array(value, shape):
    a = np.asarray(value, dtype=np.float64)
    if a.shape != tuple(shape):
        raise ValueError(f"Expected shape {tuple(shape)}, received {a.shape}")
    return np.ascontiguousarray(a)


def ptr(a):
    return a.ctypes.data_as(DP) if a is not None else None


def view(pointer, shape, owner):
    n = int(np.prod(shape))
    if not n:
        return np.empty(shape, dtype=np.float64)
    if not pointer:
        raise ValueError("Null native array")
    buffer = (D * n).from_address(C.addressof(pointer.contents))
    # numpy's base memoryview retains this ctypes object, including its owner.
    buffer._tmt_owner = owner
    return np.ctypeslib.as_array(buffer).reshape(shape)


def mapping(name, pointer):
    m = Mappings()
    call(name, pointer, C.byref(m))
    try:
        return {m.items[i].name.text(): m.items[i].index for i in range(m.size)}
    finally:
        lib.DeleteMappings(C.byref(m))
