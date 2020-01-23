import torch
import numbers
from functools import wraps
from . import masking
from . import monkey_patch
import collections
import os

from . import utils
from . import creation

import nestedtensor
from nestedtensor import _C

# Set this flag to true, if you want to enable additional verifications.
DEBUG = int(os.getenv("DEBUG", 1))


# -------------------------NestedTensor core---------------------------
class Batch(object):
    # TODO: Support for mixed entries
    # Expects a _C.NestedTensor
    def __init__(self, impl):
        self._impl = impl
        self._first_tensor = impl.unbind()[0]

    # --- impl forward ---

    def __getattr__(self, name):
        return getattr(self._first_tensor, name)

    def dim(self):
        return self._impl.dim()

    def is_pinned(self):
        return self._impl.is_pinned()

    def __len__(self):
        """
        The number of entries in the list ```self``` represents.
        """
        return self._impl.__len__()

    def is_contiguous(self):
        return self._impl.is_contiguous()

    def contiguous(self):
        # TODO: Test autograd support
        return Batch(NestedTensor(self._impl.contiguous()))

    def size(self):
        return tuple(self._impl.nested_size().unbind())

    def __str__(self):
        return self._impl.__str__()

    def __repr__(self):
        # TODO: This relies on the fact that repr is not implemented compliant with
        # the purpose of repr for torch.Tensor. Therefore returning str is ok.
        return self.__str__()

    # --- dependent on impl ends ---

    def __torch_function__(self, func, args_=(), kwargs_=None):
        batch_len = None
        for arg in args_:
            if isinstance(arg, Batch):
                batch_len = len(arg)
                break

        if not batch_len:
            for k, v in kwargs_:
                if isinstance(v, Batch):
                    batch_len = len(v)

        assert batch_len is not None

        # TODO: Verify, but I think this causes views...
        args = []
        for arg in args_:
            if torch.is_tensor(arg):
                args.append(creation.batch(batch_len * [arg]))._impl
        kwargs = {}
        for k, v in kwargs_:
            if torch.is_tensor(v):
                kwargs[k] = creation.batch(batch_len * [v])._impl

        return nestedtensor.tensorwise()(func)(args, kwargs)

    def __getitem__(self, *args, **kwargs):
        return self.unbind().__getitem__(*args, **kwargs)

    def __iter__(self):
        return iter(self.unbind())
