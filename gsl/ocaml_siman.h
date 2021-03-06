#include <stdio.h>
#include <string.h> // for memcpy

// Prevent OCaml from exporting short macro names.
#define CAML_NAME_SPACE 1

#include <caml/callback.h>
#include <caml/mlvalues.h>
#include <caml/memory.h> // CAMLreturn
#include <caml/alloc.h> // caml_copy

#include <gsl_errno.h>
#include <gsl_math.h> // log1p
#include <gsl_statistics_double.h>
#include <gsl_sf_erf.h>
#include <gsl_fit.h> // gsl_fit_linear
#include <gsl_integration.h>
#include <gsl_multifit_nlinear.h>
#include <gsl_cdf.h>
#include <gsl_sf_gamma.h> // gsl_sf_fact
#include <gsl_randist.h> // gsl_ran_binomial_pdf

#include <gsl_siman.h> // simulated annealing
#include <gsl_rng.h> // random number generator

struct ocaml_siman_callbacks {
  value copy;
  value energy;
  value step;
  value dist;
};

struct ocaml_siman_object {
  struct ocaml_siman_callbacks* callbacks;
  value state;
};

double ocaml_siman_energy (struct ocaml_siman_object* xp) {
  CAMLparam0 ();
  CAMLreturnT (double, Double_val (caml_callback (xp->callbacks->energy, xp->state)));
}

void ocaml_siman_step (
  const gsl_rng* rng,
  struct ocaml_siman_object* xp,
  double step_size
) {
  CAMLparam0 ();
  double step = (2 * gsl_rng_uniform (rng) - 1) * step_size;
  caml_callback2 (xp->callbacks->step, xp->state, caml_copy_double (step));
  CAMLreturn0;
}

double ocaml_siman_dist (
  struct ocaml_siman_object* xp,
  struct ocaml_siman_object* yp
) {
  CAMLparam0 ();
  CAMLreturnT (double, Double_val (caml_callback2 (xp->callbacks->dist, xp->state, yp->state)));
}

void ocaml_siman_copy_into (
  struct ocaml_siman_object* src,
  struct ocaml_siman_object* dst
) {
  CAMLparam0 ();
  dst->callbacks = src->callbacks;
  dst->state = caml_callback (src->callbacks->copy, src->state);
  caml_register_global_root (&(*dst).state);
  CAMLreturn0;
}

void* ocaml_siman_copy (struct ocaml_siman_object* src) {
  struct ocaml_siman_object* dst = malloc (sizeof (struct ocaml_siman_object));
  ocaml_siman_copy_into (src, dst);
  return dst;
}

void ocaml_siman_destroy (struct ocaml_siman_object* xp) {
  free (xp);
}

void ocaml_siman_print (struct ocaml_siman_object* xp) {
  CAMLparam0 ();
  CAMLreturn0;
}

CAMLprim value ocaml_siman_solve (
  value copy,
  value energy,
  value step,
  value dist,
  value init
) {
  CAMLparam5 (copy, energy, step, dist, init);

  struct ocaml_siman_callbacks callbacks = {
    .copy   = copy,
    .energy = energy,
    .step   = step,
    .dist   = dist
  };
  caml_register_global_root (&callbacks.copy);
  caml_register_global_root (&callbacks.energy);
  caml_register_global_root (&callbacks.step);
  caml_register_global_root (&callbacks.dist);

  struct ocaml_siman_object obj = {
    .callbacks = &callbacks,
    .state     = init
  };
  caml_register_global_root (&obj.state);

  gsl_rng_env_setup ();
  gsl_rng* rng = gsl_rng_alloc (gsl_rng_default);

  gsl_siman_params_t params = {
    .n_tries = 200, // 200
    .iters_fixed_T = 1000, // 1000
    .step_size = 1,
    .k = 1.0, // Boltzman constant
    .t_initial = 0.008, // initial temperature
    .mu_t = 1.003,
    .t_min = 2.0e-6 // minimum temperature
  };

  gsl_siman_solve (
    rng,
    &obj,
    (gsl_siman_Efunc_t) ocaml_siman_energy,
    (gsl_siman_step_t) ocaml_siman_step,
    (gsl_siman_metric_t) ocaml_siman_dist,
    NULL,
    (gsl_siman_copy_t) ocaml_siman_copy_into,
    (gsl_siman_copy_construct_t) ocaml_siman_copy,
    (gsl_siman_destroy_t) ocaml_siman_destroy,
    0, // element size - signal variable.
    params
  );

  gsl_rng_free (rng);

  CAMLreturn (obj.state);
}
