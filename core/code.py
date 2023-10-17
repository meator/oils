#!/usr/bin/env python2
"""
code.py: User-defined funcs and procs
"""
from __future__ import print_function

from _devbuild.gen.runtime_asdl import value, value_t, scope_e, lvalue
from _devbuild.gen.syntax_asdl import ArgList, proc_sig, proc_sig_e

from core import error
from core.error import e_die
from core import state
from core import vm
from frontend import typed_args
from mycpp.mylib import log
from ysh import expr_eval

from typing import List, cast, TYPE_CHECKING
if TYPE_CHECKING:
    from _devbuild.gen.syntax_asdl import command, loc_t
    from _devbuild.gen.runtime_asdl import Proc
    from core import ui
    from osh import cmd_eval

_ = log


class UserFunc(vm._Callable):
    """A user-defined function."""

    def __init__(self, name, node, mem, cmd_ev):
        # type: (str, command.Func, state.Mem, cmd_eval.CommandEvaluator) -> None
        self.name = name
        self.node = node
        self.cmd_ev = cmd_ev
        self.mem = mem

    def Call(self, args):
        # type: (typed_args.Reader) -> value_t
        pos_args = args.RestPos()
        num_args = len(pos_args)
        num_params = len(self.node.pos_params)

        # TODO: this is the location of 'func', not the CALL.  Should add that
        # location to typed_args.Reader

        blame_loc = self.node.keyword

        if self.node.rest_of_pos:
            if num_args < num_params:
                raise error.TypeErrVerbose(
                    "%s() expects at least %d arguments but %d were given" %
                    (self.name, num_params, num_args), blame_loc)
        elif num_args != num_params:
            raise error.TypeErrVerbose(
                "%s() expects %d arguments but %d were given" %
                (self.name, num_params, num_args), blame_loc)

        named_args = args.RestNamed()
        args.Done()
        num_args = len(named_args)
        num_params = len(self.node.named_params)
        if num_args != num_params:
            raise error.TypeErrVerbose(
                "%s() expects %d named arguments but %d were given" %
                (self.name, num_params, num_args), blame_loc)

        # Push a new stack frame
        with state.ctx_FuncCall(self.cmd_ev.mem, self):

            # TODO: Handle default args.  Evaluate them here or elsewhere?

            num_args = len(self.node.pos_params)
            for i in xrange(0, num_args):
                pos_param = self.node.pos_params[i]
                lval = lvalue.Named(pos_param.name, pos_param.blame_tok)

                pos_arg = pos_args[i]
                self.mem.SetValue(lval, pos_arg, scope_e.LocalOnly)

            if self.node.rest_of_pos:
                p = self.node.rest_of_pos
                lval = lvalue.Named(p.name, p.blame_tok)

                rest_val = value.List(pos_args[num_args:])
                self.mem.SetValue(lval, rest_val, scope_e.LocalOnly)

            # TODO: pass named args

            try:
                self.cmd_ev._Execute(self.node.body)

                return value.Null  # implicit return
            except vm.ValueControlFlow as e:
                return e.value
            except vm.IntControlFlow as e:
                raise AssertionError('IntControlFlow in func')

        raise AssertionError('unreachable')


def BindProcArgs(
        proc,  # type: Proc
        argv,  # type: List[str]
        arg0_loc,  # type: loc_t
        args,  # type: ArgList
        mem,  # type: state.Mem
        errfmt,  # type: ui.ErrorFormatter
        expr_ev,  # type: expr_eval.ExprEvaluator
):
    # type: (...) -> int
    UP_sig = proc.sig
    if UP_sig.tag() != proc_sig_e.Closed:  # proc is-closed ()
        return 0

    sig = cast(proc_sig.Closed, UP_sig)

    try:
        t = typed_args.ReaderFromArgv(argv, args, expr_ev)

        nwords = t.NumWords()
        for i, p in enumerate(sig.word_params):
            val = None  # type: value_t

            # proc p(out Ref)
            is_out_param = (p.type is not None and p.type.name == 'Ref')
            #log('is_out %s', is_out_param)
            param_name = p.name  # may get hidden __

            if i >= nwords:
                if not p.default_val:
                    t.Word()  # Should raise
                    assert False, "unreachable"

                # Not sure how this will behave... disallowing it for now
                assert not is_out_param, "Out params cannot have default values"

                val = proc.defaults[i]

            else:
                # TODO: can we pass a default to t.Word()? Would simplify everything
                arg_str = t.Word()

                # If we have myproc(p), and call it with myproc :arg, then bind
                # __p to 'arg'.  That is, the param has a prefix ADDED, and the arg
                # has a prefix REMOVED.
                #
                # This helps eliminate "nameref cycles".
                if is_out_param:
                    # TODO: should we move this into typed_args.Reader? t.WordRef()?
                    param_name = '__' + param_name

                    if not arg_str.startswith(':'):
                        # TODO: Point to the exact argument.  We got argv but not
                        # locations.
                        e_die(
                            'Ref param %r expected arg starting with colon : but got %r'
                            % (p.name, arg_str))

                    arg_str = arg_str[1:]

                val = value.Str(arg_str)

            if is_out_param:
                flags = state.SetNameref
            else:
                flags = 0

            mem.SetValue(lvalue.Named(param_name, p.blame_tok),
                         val,
                         scope_e.LocalOnly,
                         flags=flags)

        if sig.rest_of_words:
            rw = sig.rest_of_words
            items = [value.Str(x)
                     for x in t.RestWords()]  # type: List[value_t]
            val = value.List(items)

            mem.SetValue(lvalue.Named(rw.name, rw.blame_tok), val,
                         scope_e.LocalOnly)

        npos = t.NumPos()
        for i, p in enumerate(sig.pos_params):
            if i >= npos and p.default_val:
                # TODO: can we cache this? would require dependency checking...
                val = expr_ev.EvalExpr(p.default_val, p.blame_tok)
            else:
                val = t.PosValue()

            mem.SetValue(lvalue.Named(p.name, p.blame_tok), val,
                         scope_e.LocalOnly)

        if sig.rest_of_pos:
            rp = sig.rest_of_pos
            rest_pos = t.RestPos()
            val = value.List(rest_pos)

            mem.SetValue(lvalue.Named(rp.name, rp.blame_tok), val,
                         scope_e.LocalOnly)

        for n in sig.named_params:
            default_ = None  # type: value_t
            if n.default_val:
                default_ = expr_ev.EvalExpr(n.default_val, n.blame_tok)

            val = t.NamedValue(n.name, default_)

            mem.SetValue(lvalue.Named(n.name, n.blame_tok), val,
                         scope_e.LocalOnly)

        if sig.rest_of_named:
            rn = sig.rest_of_named
            rest_named = t.RestNamed()
            val = value.Dict(rest_named)

            mem.SetValue(lvalue.Named(rn.name, rn.blame_tok), val,
                         scope_e.LocalOnly)

        if sig.block_param:
            b = t.Block()
            val = value.Command(b)

            mem.SetValue(
                lvalue.Named(sig.block_param.name, sig.block_param.blame_tok),
                val, scope_e.LocalOnly)

        t.Done()
    except error._ErrorWithLocation as err:
        err.location = arg0_loc  # TEMP: We should be passing locs to Reader
        errfmt.PrettyPrintError(err)
        return 2

    return 0
