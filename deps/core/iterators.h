#ifndef ITERATORS_H_H4OGFHAN
#define ITERATORS_H_H4OGFHAN

#define $foreach_hashed(T, V, HM) \
    for ( T V = HM; V != NULL; V = (T)V->hh.next )

#define $foreach( item, array )                                                \
    for ( int keep = 1, count = 0, size = sizeof( array ) / sizeof *( array ); \
          keep && count != size; keep = !keep, count++ )                       \
        for ( item = (array)+count; keep; keep = !keep )

#endif /* end of include guard: ITERATORS_H_H4OGFHAN */
