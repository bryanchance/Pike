/*
 * Null policy-manager for the generic Caching system
 * by Francesco Chemolli <kinkie@roxen.com>
 * (C) 2000 Roxen IS
 *
 * $Id: Null.pike,v 1.2 2000/09/26 18:59:11 hubbe Exp $
 *
 * This is a policy manager that doesn't actually expire anything.
 * It is useful in multilevel and/or network-based caches.
 */

#pike __VERSION__

void expire (Cache.Storage storage) {
  /* empty */
}
