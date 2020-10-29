# COA$TGUARD - for a safer C.

"The great sea makes one a great sceptic"

Programming in C can be tricky. Yes, you get the superpowers of
low level programming, but with great power comes great.. well, you know.

Two important disclaimers:
* There is no magic here. COA$TGUARD is merely encouraging a programming
  pattern that can make your code safer.
* If you dislike using goto, this is probably not going to make you happy. 

## Features

COA$STGUARD tries to enfore good error handling habits by:

* Allowing you to easily pair status with function return values
* Providing status inspection and value unwrapping macros
* Providing additional condition testing macros and logging facilities

### Quick example:

``` C
$typedef(distance_in_meters) wrapped_distance_in_meters;

wrapped_distance_in_meters
get_distance_to_planet()
{
    ...
    return (wrapped_distance_in_meters){calculated_distance_value}
}

void
some_function(void)
{
    wrapped_distance_in_meters d = get_distance_to_planet();
    distance_in_meters d = $unwrap(d);
    ...
    return;
error:
    // error handling or panic //
}
```

## Programming Guide

### Type definition

```
$typedef(foo) wrapped_foo;
```

### Wrapping values

Instead of representing errors as NULL pointers or as error codes, COA$TGUARD
encourages you to return wrapped values:

```
wrapped_foo
bar()
{
    ...
all_is_well:
    return (wrapped_foo){foo_value};
all_is_bad:
    return $invalid(wrapped_foo, "do not use, content is invalid!")
}
```

### Inspection

Wrapped values require inpsection before using:

```
```


### Unwrapping

There's also a combined inspection and assignment macro:

```
// let's get a wrapped_foo. Since this is a wrapped foo, we're not
// actually sure there is a valid foo inside:
wrapped_foo wf = some_function_returning_wrapped_foo();

// When we unwrap wf, we do expect to usally get a valid foo, but in case
// we don't, $unwrap will goto error (or to a different label if you specify it)
foo f = $unwrap(wf);

...

// This is where $unwrap will goto in case we don't have a valid foo in the
// wrapped return value.
error:
    // Some form of error handling or panic goes here.
```

