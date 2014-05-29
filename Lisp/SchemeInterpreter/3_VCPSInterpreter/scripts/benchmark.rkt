(define (timing f loop)
  (define (iter n)
    (if (= 1 n)
      (f)
      (begin (f) (iter (- n 1))))
    )
  (define start (current-inexact-milliseconds))
  (iter loop)
  (printf "~a\n" (- (current-inexact-milliseconds) start))
  )
(define (fib n)
  (define (iter n a b)
    (if (= 0 n)
      a
      (iter (- n 1) b (+ a b)))
    )
  (iter n 0 1)
  )
(timing (lambda () (fib 30)) 5000)
(timing (lambda () (map (curry 2 * 2) (range 256))) 300)