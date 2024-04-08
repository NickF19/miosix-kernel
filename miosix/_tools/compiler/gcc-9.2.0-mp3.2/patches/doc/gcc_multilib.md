# Multilib configuration with config file fragments in GCC

Even though the documentation in https://gcc.gnu.org/onlinedocs/gccint/Target-Fragment.html
is fairly comprehensive about how config file fragments work, it is missing
a high-level overview of what is happening and leaves out important details,
which might make the process of updating these files a bit baffling.

The config file fragments to run are selected by the config.gcc shell script,
depending on the specified target triple, matched via shell script globs.
The list of fragments is stored in the tmake_file environment variable, which
through the magic of autoconf/automake gets copied in the materialized
Makefile, where they are included just before the declaration of the recipes
related to target-dependent files. Therefore, these "config" file fragments are
actually Makefile includes, and do not actually run at config time. Whatever.

The config fragments set the following variables related to multilib:

 - `MULTILIB_OPTIONS`, `MULTILIB_DIRNAMES`: base set of multilibs
 - `MULTILIB_EXCEPTIONS`: blacklist to be applied to the base set
 - `MULTILIB_REQUIRED`: whitelist to be applied to the base set after the blacklist
 - `MULTILIB_MATCHES`: specifies aliases for single options
 - `MULTILIB_REUSE`: specifies aliases for single option combinations

These variables are processed by a Python script called `genmultilib`, which
in turn produces a header file called `multilib.h` which is then included
by the code of GCC. This mechanism effectively "bakes" the multilib directory
structure in the GCC executable.

## Multilib option expansion

The `MULTILIB_OPTIONS` variable is basically interpreted as a list of
sets, where the number of sets is defined by the number of options separated by
spaces, and slashes (/) separe each item in the set. Each set also implicitly
includes the empty string (for the case in which the option is not specified).

Therefore if

```
MULTILIB_OPTIONS=mthumb march=armv6s-m/march=armv7-m/march=armv7e-m mfloat-abi=hard/mfloat-abi=softfp
```

then there are 3 sets with size 2, 4, 3 respectively:

```
""          ""                   ""
"mthumb"    "march=armv6s-m"     "mfloat-abi=hard"
            "march=armv7-m"      "mfloat-abi=softfp"
            "march=armv7e-m"
```

The multilib list is generated by cartesian product of the sets, resulting in:

```
"","",""
"","","mfloat-abi=hard"
"","","mfloat-abi=softfp"
"","march=armv6s-m",""
"","march=armv6s-m","mfloat-abi=hard"
"","march=armv6s-m","mfloat-abi=softfp"
"","march=armv7-m",""
"","march=armv7-m","mfloat-abi=hard"
"","march=armv7-m","mfloat-abi=softfp"
"","march=armv7e-m",""
"","march=armv7e-m","mfloat-abi=hard"
"","march=armv7e-m","mfloat-abi=softfp"
"mthumb","",""
"mthumb","","mfloat-abi=hard"
"mthumb","","mfloat-abi=softfp"
"mthumb","march=armv6s-m",""
"mthumb","march=armv6s-m","mfloat-abi=hard"
"mthumb","march=armv6s-m","mfloat-abi=softfp"
"mthumb","march=armv7-m",""
"mthumb","march=armv7-m","mfloat-abi=hard"
"mthumb","march=armv7-m","mfloat-abi=softfp"
"mthumb","march=armv7e-m",""
"mthumb","march=armv7e-m","mfloat-abi=hard"
"mthumb","march=armv7e-m","mfloat-abi=softfp"
```

And now you know why this way of deciding which multilibs to build wasn't
exactly the best one... and they introduced `MULTILIB_EXCEPTIONS`,
`MULTILIB_REQUIRED`, `MULTILIB_MATCHES` and `MULTILIB_REUSE` to patch it up.
All these variables take the list above and modify it. `MULTILIB_REQUIRED`
modifies the combinations GCC build time, while the others are implemented
at GCC runtime.

The `genmultilib` script flattens the sets even more, by repeating all options
that were *not* chosen, prefixed by a "!", producing basically an (inefficient)
one-hot encoding of the above. Therefore the list above becomes:

```
"!mthumb !march=armv6s-m !march=armv7-m !march=armv7e-m !mfloat-abi=hard !mfloat-abi=softfp"
"!mthumb !march=armv6s-m !march=armv7-m !march=armv7e-m mfloat-abi=hard !mfloat-abi=softfp"
"!mthumb !march=armv6s-m !march=armv7-m !march=armv7e-m !mfloat-abi=hard mfloat-abi=softfp"
"!mthumb march=armv6s-m !march=armv7-m !march=armv7e-m !mfloat-abi=hard !mfloat-abi=softfp"
"!mthumb march=armv6s-m !march=armv7-m !march=armv7e-m mfloat-abi=hard !mfloat-abi=softfp"
"!mthumb march=armv6s-m !march=armv7-m !march=armv7e-m !mfloat-abi=hard mfloat-abi=softfp"
...
```

You get the idea...

## Multilib selection process

When choosing a multilib, GCC first normalizes (in a target-dependent way)
the command line options related to the architecture to a certain degree
(mainly adds -march=... if it has been implied by a more complex sequence of
options, and also adds any default option if it has not been specifically
overridden) and then picks the multilib directory via the `set_multilib_dir()`
function (in gcc/gcc.c).

This function first check if there is an exclusion (`MULTILIB_EXCEPTIONS`)
that matches all the specified arguments. In that case it quits immediately,
which results in not choosing any multilib.
Otherwise, it checks every available multilib and alias (`MULTILIB_REUSE`)
for a match with the given options. Options in the multilib specification and
the command line are compared via plain old string comparison (!).
The algorithm looks like this, in pseudocode (assuming I didn't read it wrong,
the original implementation of the logic is unnecessarily convoluted):

```
for combination in multilibs:
  ok = true
  for option in combination:
    # gcc/gcc.c:8944 (gcc 9.2.0)
    if option was specified because it's a default:
      continue
    # gcc/gcc.c:8930 (gcc 9.2.0)
    in_cmd_line = option was specified in the command line
    should_be_in_cmd_line = option is specified for this multilib (not prefixed with !)
    if in_cmd_line != should_be_in_cmd_line:
      ok = false
      break
  # gcc/gcc.c:8951 (gcc 9.2.0)
  if ok:
    return directory of this combination
# no multilib found
return root lib directory
```

In other words, options that are not contemplated by the multilib configuration
are ignored. Amongst the options contemplated, a multilib matches if all
non-default options given to gcc (after normalization) match exactly with the
ones that were enabled for that multilib.

Note that options set as default via defines (for example `TARGET_DEFAULT_FLOAT_ABI`)
do NOT count as a default option for the logic above, as these defaults never
appear as argument strings in a gcc internal command line. I leave any comment
to the reader.

## Can I have two options that always appear together be associated with one directory level instead of two?

Example: `-fpie -msingle-pic-base` are either both enabled or both disabled for
Miosix multilibs. We want to use a single directory level `processes` for that
pair of options instead of two levels.

Putting the two options in quotes will never work because the quotes will
be inconsistently expanded by the various scripts. I didn't even try this
approach because it sounds so hopeless. (if you have one day to waste you can
try it out!)

What I tried is using "." for one of the directories like this:

```
MULTILIB_OPTIONS    += fpie msingle-pic-base
MULTILIB_DIRNAMES   += processes .
```

which incredibly is fine for GCC... but not for newlib because for some
(probably Makefile-related) reason at one point it counts the number of
path components and replaces them with ".." to get to the root and... well yeah,
you can guess what happens :(

So the answer is no, you can't do this. That's sad.