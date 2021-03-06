#lang racket

(require compatibility/mlist)
;------------------------------
; insertion-sort
(define (insert v l)
  (if (empty? l)
    (list v)
    (let ((first (car l))(rest (cdr l)))
      (cond
        ((<= v first) (cons v l))
        ((> v first) (cons first (insert v rest))))))
  )

(define (insertion-sort l)
  (if (empty? l)
    empty
    (insert (car l) (insertion-sort (cdr l))))
  )

;------------------------------
; bubble-sort
(define (bubble l)
  (cond 
    ((empty? l) empty)
    ((empty? (cdr l)) l)
    (else 
      (let ((first (car l))(rest (bubble (cdr l))))
        (if (<= first (car rest))
          (cons first rest)
          (cons (car rest) (cons first (cdr rest)))))))
  )

(define (bubble-sort l)
  (if (empty? l)
    empty
    (let ((newl (bubble l)))
      (cons (car newl) (bubble-sort (cdr newl)))))
  )

;------------------------------
; quick-sort

; curry is too slow, because it should concat the args every time
(define (quick-sort-curry l)
  (if (or (empty? l) (empty? (cdr l)))
    l
    (let ((first (car l))(rest (cdr l)))
      (append (quick-sort-curry (filter (curry > first) rest))
              (cons first (quick-sort-curry (filter (curry <= first) rest))))))
  )

(define (quick-sort-classic l)
  (if (or (empty? l) (empty? (cdr l)))
    l
    (let ((first (car l))(rest (cdr l)))
      (append (quick-sort-classic (filter (lambda (i) (< i first)) rest))
              (cons first (quick-sort-classic (filter (lambda (i) (>= i first)) rest))))))
  )

(define (quick-sort-continuation l)
  (define (sort l continue)
    (if (null? l)
      (continue empty)
      (let ((first (car l))(rest (cdr l)))
        (let ((left (filter (lambda (i) (< i first)) rest))(right (filter (lambda (i) (>= i first)) rest)))
          (sort left (lambda (ordered) 
                       (sort right (lambda (ordered-2) 
                                     (continue (append ordered (cons first ordered-2))))))))))
    )
  (sort l (lambda (ordered) (set! l ordered)))
  l)

(define (quick-sort-3way l)
  (if (or (empty? l) (empty? (cdr l)))
    l 
    (let ((first (car l))(rest (cdr l)))
      (append (quick-sort-3way (filter (lambda (i) (< i first)) rest))
              (cons first (filter (lambda (i) (= i first)) rest))
              (quick-sort-3way (filter (lambda (i) (> i first)) rest)))))
  )

(define (quick-sort-partition l)
  (define (partition l depth v left right)
    (if (empty? l)
      (append (quick-sort-partition left) (cons v (quick-sort-partition right)))
      (if (or (< (car l) v) (and (= (car l) v) (= 0 (remainder depth 2))))
        (partition (cdr l) (add1 depth) v (cons (car l) left) right)
        (partition (cdr l) (add1 depth) v left (cons (car l) right)))
      ))
  (if (empty? l)
    l
    (partition (cdr l) 0 (car l) empty empty))
  )

(define (quick-sort-partition-3way l)
  (define (partition l v left mid right)
    (if (empty? l)
      (append (quick-sort-partition-3way left) (append (cons v mid) (quick-sort-partition-3way right)))
      (cond
        ((< (car l) v) (partition (cdr l) v (cons (car l) left) mid right))
        ((> (car l) v) (partition (cdr l) v left mid (cons (car l) right)))
        (else (partition (cdr l) v left (cons (car l) mid) right))))
    )
  (if (empty? l)
    l
    (partition (cdr l) (car l) empty empty empty))
  )

; use mpair to avoid redundant memory allocation, especially for mappend!
(define (quick-sort-mutable l) 
  (define (iter l)
    (define (partition l depth v left right)
      (if (eq? empty l)
        (mappend! (iter left) (mcons v (iter right)))
        (let ((rest (mcdr l)))
          (if (or (< (mcar l) v) (and (= (mcar l) v) (= 0 (remainder depth 2))))
            (begin (set-mcdr! l left) (partition rest (add1 depth) v l right))
            (begin (set-mcdr! l right) (partition rest (add1 depth) v left l))))))
    (if (eq? empty l)
      l
      (partition (mcdr l) 0 (mcar l) empty empty))
    )

  (mlist->list (iter (list->mlist l)))
  )

(define (quick-sort-builtin l)
  (sort l <)
  )
;------------------------------
; merge-sort

(define (merge l r)
  (cond
    ((empty? l) r)
    ((empty? r) l)
    (else 
      (let ((lfirst (car l))(lrest (cdr l))(rfirst (car r))(rrest (cdr r)))
        (cond
          ((< lfirst rfirst) (cons lfirst (merge lrest r)))
          ((> lfirst rfirst) (cons rfirst (merge l rrest)))
          (else (cons lfirst (cons rfirst (merge lrest rrest))))))))
  )

(define (merge-sort l)
  (define (prefix-merge-sort l n)
    (cond
      ((= n 0) (cons empty l))
      ((= n 1) (cons (list (car l)) (cdr l)))
      (else 
        (let ((half (quotient n 2)))
          (let ((left (prefix-merge-sort l half)))
            (let ((right (prefix-merge-sort (cdr left) (- n half))))
              (cons (merge (car left) (car right)) (cdr right)))))))
    )
  (car (prefix-merge-sort l (length l)))
  )

(define (merge-sort-continuation l)
  (define (prefix-merge-sort l n continue)
    (cond 
      ((= 0 n) (continue empty l))
      ((= 1 n) (continue (list (car l)) (cdr l)))
      (else
        (let ((half (quotient n 2)))
          (prefix-merge-sort l half 
                             (lambda (ordered rest) 
                               (prefix-merge-sort rest (- n half) 
                                                  (lambda (ordered-2 rest-2) (continue (merge ordered ordered-2) rest-2))))))))
    )
  (prefix-merge-sort l (length l) (lambda (ordered rest) (set! l ordered)))
  l)
;------------------------------
; bst-sort
(define (bst-insert v node)
  (if (empty? node)
    (list v empty empty)
    (let ((nv (car node))(left (cadr node))(right (caddr node)))
      (if (<= v nv)
        (list nv (bst-insert v left) right)
        (list nv left (bst-insert v right)))))
  )

(define (bst-infix-traverse node)
  (if (empty? node)
    empty
    (let ((nv (car node))(left (cadr node))(right (caddr node)))
      (append (bst-infix-traverse left) 
              (cons nv (bst-infix-traverse right)))))
  )

(define (bst-sort l)
  (bst-infix-traverse (foldl bst-insert empty l))
  )

;------------------------------
; test
(define test-datas (map 
                     (lambda (len) (build-list len (lambda (i) (random len))))
                     (list 1 3 4 5 7 8 9 10 11 15 16 32 33 64 128 254 255 256 257)))
(define (test f)
  (for-each (lambda (data) (if (equal? (f data) (sort data <)) 'ok (error "not ordered!" f))) test-datas))
(for-each test 
          (list 
            insertion-sort
            bubble-sort
            quick-sort-curry
            quick-sort-classic
            quick-sort-continuation
            quick-sort-3way
            quick-sort-partition
            quick-sort-partition-3way
            quick-sort-mutable
            quick-sort-builtin
            merge-sort
            merge-sort-continuation
            bst-sort
            ))
;------------------------------
; benchmark
(define benchmark-datas 
  (list 
    (build-list 5000 (lambda (i) (random 5000))) ; basic data
    (build-list 10000 (lambda (i) (random 10000))) ; double size
    (build-list 20000 (lambda (i) (random 20000))) 
    (build-list 40000 (lambda (i) (random 40000)))
    (build-list 400000 (lambda (i) (random 400000)))   
    (build-list 1000000 (lambda (i) (random 1000000)))  ; huge randoms
    (build-list 10000 (lambda (i) (if (= 0 (remainder i 2)) 5000 (random 10000)))) ; half repeat
    (build-list 3000 identity) ; ordered
    ))

(define (benchmark max-len f)
  (printf "~a:\n" f)
  (for-each (lambda (data) 
              (let ((start-time (current-inexact-milliseconds)))
                (f data)
                (printf "\t ~a -> ~a\n" (length data) (- (current-inexact-milliseconds) start-time))))
            (filter (lambda (data) (<= (length data) max-len)) benchmark-datas))
  )

(for-each (lambda (pair) (benchmark (car pair) (cdr pair)))
          (list 
            (cons 20000 insertion-sort)
            (cons 20000 bubble-sort)
            (cons 500000 quick-sort-curry)
            (cons 1000000 quick-sort-classic)
            (cons 1000000 quick-sort-continuation)
            (cons 1000000 quick-sort-3way)
            (cons 1000000 quick-sort-partition)
            (cons 1000000 quick-sort-partition-3way)
            (cons 1000000 quick-sort-mutable)
            (cons 1000000 quick-sort-builtin)
            (cons 1000000 merge-sort)
            (cons 1000000 merge-sort-continuation)
            (cons 1000000 bst-sort)
            ))
