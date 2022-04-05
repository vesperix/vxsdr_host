#ifndef FFT_HPP
#define FFT_HPP

#include <cmath>
#include <cstdlib>
#include <vector>
#include <exception>
#include <complex>
#include <fftw3.h>

//FIXME: need to test OpenMP FFT before enabling!!!!
//#define USE_OPEN_MP _OPENMP

template <typename T> class fft {
  size_t nfft;
  fftw_complex *x = nullptr;
  fftw_plan pf = nullptr;
  fftw_plan pb = nullptr;
#if defined(USE_OPEN_MP)
  int maxthreads = 1; // will make changes for multithreaded FFTs later
#endif
  bool do_measure = true;
  bool register_handlers = false;
 public:
  fft() {
    nfft = 0;
  };
  fft(const size_t nfft, const bool measure = true);
  fft(const fft& orig);
//  fft(fft&& orig);      // FIXME: add move constructor
  fft& operator=(const fft& rhs);
//  fft& operator=(fft && rhs);  // FIXME: add move assignment
  size_t size() const {
    return nfft;
  }
  void resize(const size_t new_nfft, const bool measure = true);
  void forward(std::vector< std::complex<T> > &);
  void forward(const std::vector< std::complex<T> > &, std::vector< std::complex<T> > &);
  void inverse(std::vector< std::complex<T> > &);
  void inverse(const std::vector< std::complex<T> > &, std::vector< std::complex<T> > &);
  ~fft() noexcept;
};

template <> class fft<float> {
  size_t nfft;
  fftwf_complex *x = nullptr;
  fftwf_plan pf = nullptr;
  fftwf_plan pb = nullptr;
#if defined(USE_OPEN_MP)
  int maxthreads = 1; // will make changes for multithreaded FFTs later
#endif
  bool do_measure = true;
  bool register_handlers = false;
 public:
  fft() {
    nfft = 0;
  };
  fft(const size_t nfft, const bool measure = true);
  fft(const fft& orig);
//  fft(fft&& orig);       // FIXME: add move constructor
  fft& operator=(const fft& rhs);
//  fft& operator=(fft && rhs);  // FIXME: add move assignment
  size_t size() const {
    return nfft;
  }
  void resize(const size_t new_nfft, const bool measure = true);
  void forward(std::vector< std::complex<float> > &);
  void forward(const std::vector< std::complex<float> > &, std::vector< std::complex<float> > &);
  void forward(const std::vector< std::complex<short> > &, std::vector< std::complex<float> > &);
  void forward(const std::complex<short> *, std::vector< std::complex<float> > &);
  void inverse(std::vector< std::complex<float> > &);
  void inverse(const std::vector< std::complex<float> > &, std::vector< std::complex<float> > &);
  void inverse(const std::vector< std::complex<short> > &, std::vector< std::complex<float> > &);
  void inverse(const std::complex<short> *, std::vector< std::complex<float> > &);
  ~fft() noexcept;
};

template <typename T> fft<T>::fft(const size_t n_fft, const bool measure) {
#if defined(USE_OPEN_MP)
#pragma omp critical(fft_cr_construct)
#endif
  {
    nfft = n_fft;
    do_measure = measure;
    if(register_handlers)
      std::atexit(fftw_cleanup);

    x  = fftw_alloc_complex(nfft);
    if(x == nullptr)
      throw std::bad_alloc();

#if defined(USE_OPEN_MP)
    fftw_plan_with_nthreads(maxthreads);
#endif
    if(measure) {
      pf = fftw_plan_dft_1d(nfft, x, x, FFTW_FORWARD, FFTW_MEASURE);
      pb = fftw_plan_dft_1d(nfft, x, x, FFTW_BACKWARD, FFTW_MEASURE);
    }
    else {
      pf = fftw_plan_dft_1d(nfft, x, x, FFTW_FORWARD, FFTW_ESTIMATE);
      pb = fftw_plan_dft_1d(nfft, x, x, FFTW_BACKWARD, FFTW_ESTIMATE);
    }
  }
}

inline fft<float>::fft(const size_t n_fft, const bool measure) {
#if defined(USE_OPEN_MP)
#pragma omp critical(fft_cr_float_construct)
#endif
  {
    nfft = n_fft;
    if(register_handlers)
      std::atexit(fftwf_cleanup);

    x  = fftwf_alloc_complex(nfft);
    if(x == nullptr)
      throw std::bad_alloc();

#if defined(USE_OPEN_MP)
    fftwf_plan_with_nthreads(maxthreads);
#endif
    if(measure) {
      pf = fftwf_plan_dft_1d(nfft, x, x, FFTW_FORWARD, FFTW_MEASURE);
      pb = fftwf_plan_dft_1d(nfft, x, x, FFTW_BACKWARD, FFTW_MEASURE);
    }
    else {
      pf = fftwf_plan_dft_1d(nfft, x, x, FFTW_FORWARD, FFTW_ESTIMATE);
      pb = fftwf_plan_dft_1d(nfft, x, x, FFTW_BACKWARD, FFTW_ESTIMATE);
    }
  }
}

template <typename T> fft<T>::fft(const fft<T>& orig) {
#if defined(USE_OPEN_MP)
#pragma omp critical(fft_cr_copy)
#endif
  {
    nfft = orig.nfft;
    do_measure = orig.do_measure;
    register_handlers = orig.register_handlers;
    if(register_handlers)
      std::atexit(fftw_cleanup);

    x  = fftw_alloc_complex(nfft);
    if(x == nullptr)
      throw std::bad_alloc();

#if defined(USE_OPEN_MP)
    fftw_plan_with_nthreads(maxthreads);
#endif
    if(do_measure) {
      pf = fftw_plan_dft_1d(nfft, x, x, FFTW_FORWARD, FFTW_MEASURE);
      pb = fftw_plan_dft_1d(nfft, x, x, FFTW_BACKWARD, FFTW_MEASURE);
    }
    else {
      pf = fftw_plan_dft_1d(nfft, x, x, FFTW_FORWARD, FFTW_ESTIMATE);
      pb = fftw_plan_dft_1d(nfft, x, x, FFTW_BACKWARD, FFTW_ESTIMATE);
    }
  }
}

inline fft<float>::fft(const fft<float>& orig) {
#if defined(USE_OPEN_MP)
#pragma omp critical(fft_cr_float_copy)
#endif
  {
		nfft = orig.nfft;
		do_measure = orig.do_measure;
		register_handlers = orig.register_handlers;
		if(register_handlers)
		  std::atexit(fftwf_cleanup);

		x  = fftwf_alloc_complex(nfft);
		if(x == nullptr)
		  throw std::bad_alloc();

#if defined(USE_OPEN_MP)
    fftwf_plan_with_nthreads(maxthreads);
#endif
		if(do_measure) {
		  pf = fftwf_plan_dft_1d(nfft, x, x, FFTW_FORWARD, FFTW_MEASURE);
		  pb = fftwf_plan_dft_1d(nfft, x, x, FFTW_BACKWARD, FFTW_MEASURE);
		}
		else {
		  pf = fftwf_plan_dft_1d(nfft, x, x, FFTW_FORWARD, FFTW_ESTIMATE);
		  pb = fftwf_plan_dft_1d(nfft, x, x, FFTW_BACKWARD, FFTW_ESTIMATE);
		}
  }
}

template <typename T> fft<T>& fft<T>::operator=(const fft<T>& rhs) {
  if(this != &rhs) {
#if defined(USE_OPEN_MP)
#pragma omp critical(fft_cr_equals)
#endif
    {
		  nfft = rhs.nfft;
		  do_measure = rhs.do_measure;
		  register_handlers = rhs.register_handlers;
		  if(register_handlers)
		    std::atexit(fftw_cleanup);

		  x  = fftw_alloc_complex(nfft);
		  if(x == nullptr)
		    throw std::bad_alloc();

  #if defined(USE_OPEN_MP)
      fftw_plan_with_nthreads(maxthreads);
  #endif
		  if(do_measure) {
		    pf = fftw_plan_dft_1d(nfft, x, x, FFTW_FORWARD, FFTW_MEASURE);
		    pb = fftw_plan_dft_1d(nfft, x, x, FFTW_BACKWARD, FFTW_MEASURE);
		  }
		  else {
		    pf = fftw_plan_dft_1d(nfft, x, x, FFTW_FORWARD, FFTW_ESTIMATE);
		    pb = fftw_plan_dft_1d(nfft, x, x, FFTW_BACKWARD, FFTW_ESTIMATE);
		  }
    }
  }
  return *this;
}

inline fft<float>& fft<float>::operator=(const fft<float>& rhs) {
  if(this != &rhs) {
#if defined(USE_OPEN_MP)
#pragma omp critical(fft_cr_float_equals)
#endif
    {
		  nfft = rhs.nfft;
		  do_measure = rhs.do_measure;
		  register_handlers = rhs.register_handlers;
		  if(register_handlers)
		    std::atexit(fftwf_cleanup);

		  x  = fftwf_alloc_complex(nfft);
		  if(x == nullptr)
		    throw std::bad_alloc();

#if defined(USE_OPEN_MP)
      fftwf_plan_with_nthreads(maxthreads);
#endif
		  if(do_measure) {
		    pf = fftwf_plan_dft_1d(nfft, x, x, FFTW_FORWARD, FFTW_MEASURE);
		    pb = fftwf_plan_dft_1d(nfft, x, x, FFTW_BACKWARD, FFTW_MEASURE);
		  }
		  else {
		    pf = fftwf_plan_dft_1d(nfft, x, x, FFTW_FORWARD, FFTW_ESTIMATE);
		    pb = fftwf_plan_dft_1d(nfft, x, x, FFTW_BACKWARD, FFTW_ESTIMATE);
		  }
    }
  }
  return *this;
}

template <typename T> void fft<T>::resize(const size_t new_nfft, const bool measure) {
#if defined(USE_OPEN_MP)
#pragma omp critical(fft_cr_resize)
#endif
  {
		nfft = new_nfft;
		do_measure = measure;
		if(x != nullptr) {
		  fftw_free(x);
		  fftw_destroy_plan(pf);
		  fftw_destroy_plan(pb);
		}

		x  = fftw_alloc_complex(nfft);
		if(x == nullptr)
		  throw std::bad_alloc();

#if defined(USE_OPEN_MP)
    fftw_plan_with_nthreads(maxthreads);
#endif
		if(measure) {
		  pf = fftw_plan_dft_1d(nfft, x, x, FFTW_FORWARD, FFTW_MEASURE);
		  pb = fftw_plan_dft_1d(nfft, x, x, FFTW_BACKWARD, FFTW_MEASURE);
		}
		else {
		  pf = fftw_plan_dft_1d(nfft, x, x, FFTW_FORWARD, FFTW_ESTIMATE);
		  pb = fftw_plan_dft_1d(nfft, x, x, FFTW_BACKWARD, FFTW_ESTIMATE);
		}
  }
}

inline void fft<float>::resize(const size_t new_nfft, const bool measure) {
#if defined(USE_OPEN_MP)
#pragma omp critical(fft_cr_float_resize)
#endif
  {
		nfft = new_nfft;
		do_measure = measure;
		if(x != nullptr) {
		  fftwf_free(x);
		  fftwf_destroy_plan(pf);
		  fftwf_destroy_plan(pb);
		}

		x  = fftwf_alloc_complex(nfft);
		if(x == nullptr)
		  throw std::bad_alloc();

#if defined(USE_OPEN_MP)
    fftwf_plan_with_nthreads(maxthreads);
#endif
		if(measure) {
		  pf = fftwf_plan_dft_1d(nfft, x, x, FFTW_FORWARD, FFTW_MEASURE);
		  pb = fftwf_plan_dft_1d(nfft, x, x, FFTW_BACKWARD, FFTW_MEASURE);
		}
		else {
		  pf = fftwf_plan_dft_1d(nfft, x, x, FFTW_FORWARD, FFTW_ESTIMATE);
		  pb = fftwf_plan_dft_1d(nfft, x, x, FFTW_BACKWARD, FFTW_ESTIMATE);
		}
  }
}

template <typename T> void fft<T>::forward(std::vector< std::complex<T> > &xx) {
  xx.resize(nfft, 0.0);
  for(unsigned i = 0; i < nfft; i++) {
    x[i][0] = xx[i].real();
    x[i][1] = xx[i].imag();
  }
  fftw_execute_dft(pf, x, x);
  for(unsigned i = 0; i < nfft; i++) {
    xx[i].real(x[i][0]);
    xx[i].imag(x[i][1]);
  }
}

template <typename T> void fft<T>::forward(const std::vector< std::complex<T> > &xx, std::vector< std::complex<T> > &yy) {
  size_t nmax = std::min(nfft, xx.size());
  yy.resize(nfft);
  for(unsigned i = 0; i < nmax; i++) {
    x[i][0] = xx[i].real();
    x[i][1] = xx[i].imag();
  }
  // zero-pad if needed
  for(unsigned i = xx.size(); i < nfft; i++) {
    x[i][0] = 0;
    x[i][1] = 0;
  }
  fftw_execute_dft(pf, x, x);
  for(unsigned i = 0; i < nfft; i++) {
    yy[i].real(x[i][0]);
    yy[i].imag(x[i][1]);
  }
}

template <typename T> void fft<T>::inverse(std::vector< std::complex<T> > &xx) {
  xx.resize(nfft, 0.0);
  for(unsigned i = 0; i < nfft; i++) {
    x[i][0] = xx[i].real();
    x[i][1] = xx[i].imag();
  }
  fftw_execute_dft(pb, x, x);
  for(unsigned i = 0; i < nfft; i++) {
    xx[i].real(x[i][0] / nfft);
    xx[i].imag(x[i][1] / nfft);
  }
}

template <typename T> void fft<T>::inverse(const std::vector< std::complex<T> > &xx, std::vector< std::complex<T> > &yy) {
  size_t nmax = std::min(nfft, xx.size());
  yy.resize(nfft);
  for(unsigned i = 0; i < nmax; i++) {
    x[i][0] = xx[i].real();
    x[i][1] = xx[i].imag();
  }
  // zero-pad if needed
  for(unsigned i = xx.size(); i < nfft; i++) {
    x[i][0] = 0;
    x[i][1] = 0;
  }
  fftw_execute_dft(pb, x, x);
  for(unsigned i = 0; i < nfft; i++) {
    yy[i].real(x[i][0] / nfft);
    yy[i].imag(x[i][1] / nfft);
  }
}

inline void fft<float>::forward(std::vector< std::complex<float> > &xx) {
  xx.resize(nfft, 0.0);
  for(unsigned i = 0; i < nfft; i++) {
    x[i][0] = xx[i].real();
    x[i][1] = xx[i].imag();
  }
  fftwf_execute_dft(pf, x, x);
  for(unsigned i = 0; i < nfft; i++) {
    xx[i].real(x[i][0]);
    xx[i].imag(x[i][1]);
  }
}

inline void fft<float>::forward(const std::vector< std::complex<float> > &xx, std::vector< std::complex<float> > &yy) {
  size_t nmax = std::min(nfft, xx.size());
  yy.resize(nfft);
  for(unsigned i = 0; i < nmax; i++) {
    x[i][0] = xx[i].real();
    x[i][1] = xx[i].imag();
  }
  // zero-pad if needed
  for(unsigned i = xx.size(); i < nfft; i++) {
    x[i][0] = 0;
    x[i][1] = 0;
  }
  fftwf_execute_dft(pf, x, x);
  for(unsigned i = 0; i < nfft; i++) {
    yy[i].real(x[i][0]);
    yy[i].imag(x[i][1]);
  }
}

inline void fft<float>::forward(const std::vector< std::complex<short> > &xx, std::vector< std::complex<float> > &yy) {
  size_t nmax = std::min(nfft, xx.size());
  yy.resize(nfft);
  for(unsigned i = 0; i < nmax; i++) {
    x[i][0] = xx[i].real();
    x[i][1] = xx[i].imag();
  }
  // zero-pad if needed
  for(unsigned i = xx.size(); i < nfft; i++) {
    x[i][0] = 0;
    x[i][1] = 0;
  }
  fftwf_execute_dft(pf, x, x);
  for(unsigned i = 0; i < nfft; i++) {
    yy[i].real(x[i][0]);
    yy[i].imag(x[i][1]);
  }
}

inline void fft<float>::forward(const std::complex<short> *xx, std::vector< std::complex<float> > &yy) {
  // WARNING: callers must assure that xx points to an array at least nfft  long!!!
  yy.resize(nfft);
  for(unsigned i = 0; i < nfft; i++) {
    x[i][0] = xx[i].real();
    x[i][1] = xx[i].imag();
  }
  fftwf_execute_dft(pf, x, x);
  for(unsigned i = 0; i < nfft; i++) {
    yy[i].real(x[i][0]);
    yy[i].imag(x[i][1]);
  }
}

inline void fft<float>::inverse(std::vector< std::complex<float> > &xx) {
  xx.resize(nfft, 0.0);
  for(unsigned i = 0; i < nfft; i++) {
    x[i][0] = xx[i].real();
    x[i][1] = xx[i].imag();
  }
  fftwf_execute_dft(pb, x, x);
  for(unsigned i = 0; i < nfft; i++) {
    xx[i].real(x[i][0] / nfft);
    xx[i].imag(x[i][1] / nfft);
  }
}

inline void fft<float>::inverse(const std::vector< std::complex<float> > &xx, std::vector< std::complex<float> > &yy) {
  size_t nmax = std::min(nfft, xx.size());
  yy.resize(nfft);
  for(unsigned i = 0; i < nmax; i++) {
    x[i][0] = xx[i].real();
    x[i][1] = xx[i].imag();
  }
  // zero-pad if needed
  for(unsigned i = xx.size(); i < nfft; i++) {
    x[i][0] = 0;
    x[i][1] = 0;
  }
  fftwf_execute_dft(pb, x, x);
  for(unsigned i = 0; i < nfft; i++) {
    yy[i].real(x[i][0] / nfft);
    yy[i].imag(x[i][1] / nfft);
  }
}

inline void fft<float>::inverse(const std::vector< std::complex<short> > &xx, std::vector< std::complex<float> > &yy) {
  size_t nmax = std::min(nfft, xx.size());
  yy.resize(nfft);
  for(unsigned i = 0; i < nmax; i++) {
    x[i][0] = xx[i].real();
    x[i][1] = xx[i].imag();
  }
  // zero-pad if needed
  for(unsigned i = xx.size(); i < nfft; i++) {
    x[i][0] = 0;
    x[i][1] = 0;
  }
  fftwf_execute_dft(pb, x, x);
  for(unsigned i = 0; i < nfft; i++) {
    yy[i].real(x[i][0] / nfft);
    yy[i].imag(x[i][1] / nfft);
  }
}


inline void fft<float>::inverse(const std::complex<short> *xx, std::vector< std::complex<float> > &yy) {
  // WARNING: callers must assure that xx points to an array at least nfft  long!!!
  yy.resize(nfft);
  for(unsigned i = 0; i < nfft; i++) {
    x[i][0] = xx[i].real();
    x[i][1] = xx[i].imag();
  }
  fftwf_execute_dft(pb, x, x);
  for(unsigned i = 0; i < nfft; i++) {
    yy[i].real(x[i][0] / nfft);
    yy[i].imag(x[i][1] / nfft);
  }
}

template <typename T> fft<T>::~fft() noexcept {
#if defined(USE_OPEN_MP)
#pragma omp critical(fft_cr_delete)
#endif
  {
    fftw_free(x);
    fftw_destroy_plan(pf);
    fftw_destroy_plan(pb);
  }
}

inline fft<float>::~fft() noexcept {
#if defined(USE_OPEN_MP)
#pragma omp critical(fft_cr_float_delete)
#endif
  {
    fftwf_free(x);
    fftwf_destroy_plan(pf);
    fftwf_destroy_plan(pb);
  }
}

#endif
