# Oil user feedback

# From Zulip:
#
# https://oilshell.zulipchat.com/#narrow/stream/121540-oil-discuss/topic/Experience.20using.20oil

#### setvar

# This seems to work as expected?

proc get_opt(arg, :out) {
  setvar out = $arg
}
var a = ''
get_opt a 'lol'
echo hi
## status: 1
## STDOUT:
## END

#### != operator
var a = 'bar'

# NOTE: a != foo is idiomatic)
if ($a != 'foo') {
  echo 'not equal'
}

if ($a != 'bar') {
  echo 'should not get here'
}

## STDOUT:
not equal
## END


#### Regex Literal

# This seems to work as expected?

proc get_opt(arg, :out) {
  setref out = $(write -- $arg | cut -d'=' -f2)
}

var variant = ''
var arg = '--variant=foo'
if ( arg ~ / '--variant=' <word> / ) {
  get_opt $arg :variant
  echo variant=$variant
}

## STDOUT:
variant=foo
## END

#### Julia port

# https://lobste.rs/s/ritbgc/what_glue_languages_do_you_use_like#c_nhikri
#
# See bash counterpart in spec/blog1.test.sh

git-branch-merged() {
  cat <<EOF
  foo
* bar
  baz
  master
EOF
}

# With bash-style readarray.  The -t is annoying.
git-branch-merged | while read --line {
  # BUG: var or const messes up in al oop.
  setvar line = _line.strip()  # removing leading space
  if (line != 'master' and not line.startswith('*')) {
    echo $line
  }
} | readarray -t :branches

if (len(branches) == 0) {
  echo "No merged branches"
} else {
  write git branch -D @branches
}

# With "push".  Hm read --lines isn't bad.
var branches2 = %()
git-branch-merged | while read --line {
  # TODO: Should be 'const' once we fix it?  Or 'var'?
  setvar line = _line.strip()  # removing leading space
  if (line != 'master' and not line.startswith('*')) {
    push :branches2 $line
  }
}

write -- ___  @branches2

## STDOUT:
git
branch
-D
foo
baz
___
foo
baz
## END

#### readonly in loop: explains why const doesn't work

# TODO: Might want to change const in Oil...
# bash actually prevents assignment and prints a warning, DOH.

seq 3 | while read -r line; do
  readonly stripped=${line//1/x}
  #declare stripped=${line//1/x}
  echo $stripped
done
## status: 1
## STDOUT:
x
## END
