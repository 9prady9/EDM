clear

set linesize 255
set obs 500
set seed 12345678
if c(MP) {
    qui set processor 1
}

global EDM_VERBOSITY = 1
global EDM_NTHREADS = 4
global EDM_SAVE_INPUTS = "ci-test"
cap rm ci-test.json

gen t = _n
tsset t

gen double x = 0.2 if _n==1
gen double y = 0.4 if _n==1

* Create a dynamic system
local r_x 3.625
local r_y 3.77
local beta_xy = 0.05
local beta_yx=0.4
local tau = 1
drawnorm double u1 u2
qui {
    forvalues i=2/`=_N' {
        replace x=l.x *(`r_x' *(1-l.x)-`beta_xy'*l.y) in `i'
        replace y=l.y *(`r_y' *(1-l.y)-`beta_yx'*l`tau'.x) in `i'
    }
}

* burn the first 300 observations
keep in 300/500

* Determining the complexity of the system

* identify optimal E
edm explore x, e(2/10)

edm xmap x y, k(5)

edm xmap x y, e(6) lib(8)

edm explore x, k(5) crossfold(10)

edm explore x, theta(0.2(0.1)2.0) algorithm(smap)

edm xmap x y, theta(0.2) algorithm(smap) savesmap(beta)
assert beta1_b2_rep1 !=. if _n>1

edm xmap y x, predict(x2) direction(oneway)
assert x2 !=. if _n>1

edm explore x, copredict(teste) copredictvar(y)
assert teste!=. if _n>1

edm explore z.x, tp(10)

edm xmap y x, tp(10) direction(oneway)

edm xmap y x, tp(10) copredict(testx) copredictvar(x2 y) direction(oneway)
assert testx!=. if _n>2

edm xmap y x, tp(10) copredict(testx2) copredictvar(z.x2 z.y) direction(oneway)
assert testx2 !=. if _n>2

edm xmap y x, extra(u1) tp(10) copredict(testx3) copredictvar(z.x2 z.y) direction(oneway)
assert testx3 !=. if _n>2

* check explore / xmap consistency

edm xmap l.x x, direction(oneway)
mat xmap_r=e(b)
edm explore x, full
mat explore_r =e(b)
assert xmap_r[1,1] == explore_r[1,1]

* check xmap reverse consistency

edm xmap x y
mat xmap1 = e(b)
edm xmap y x
* TODO: Fix the following asserts, which are considered syntax errors in Stata15 (on Mac).
* assert e(b)[1,1] == xmap1[1,2]
* assert e(b)[1,2] == xmap1[1,1]

tempfile basedata
qui compress
qui save `basedata', replace
* test missing data
set seed 12345678
gen double u = runiform()
drop if u<0.1
replace x = . if u<0.2
replace t=. if mod(t,19) ==1

edm explore x
edm explore x, dt

edm explore x, allowmissing
edm explore x, missingdistance(2)
edm xmap x l.x, allowmissing
edm xmap x l.x, missingdistance(2)

edm xmap x l.x, extraembed(u) allowmissing dt alg(smap) savesmap(newb) e(5)

edm xmap x l3.x, extraembed(u) allowmissing dt alg(smap) savesmap(newc) e(5) oneway dtsave(testdt)

edm explore x, extraembed(u) allowmissing dt crossfold(5)

edm explore d.x, dt

edm explore x, rep(20) ci(95)

edm xmap x y, lib(50) rep(20) ci(95)

use `basedata', clear
cap drop x_copy
cap drop x_p
cap drop xc_p
clonevar x_copy = x
edm explore x, predict(x_p) copredict(xc_p) copredictvar(x_copy) full
assert x_p!=. if _n!=1 & _n!=_N
assert xc_p!=. if _n!=1 & _n!=_N
assert x_p ==xc_p if x_p!=.
cor x_p f.x
cor xc_p f.x

cap drop x_p xc_p
edm explore x, predict(x_p) copredict(xc_p) copredictvar(x_copy) full tp(2)
sum x_p xc_p
assert xc_p!=. if x_p!=.

gen y_copy = y
edm xmap x y, tp(10) copredict(xmap_y_p) copredictvar(x_copy y_copy) direction(oneway) predict(xmap_y)
assert xmap_y_p !=. if _n>1
assert xmap_y_p == xmap_y if xmap_y !=.

preserve
drop if mod(t,17)==1
//copredict with dt
edm xmap x y, dt copredict(xmap_y_p_dt) copredictvar(x_copy y_copy) direction(oneway) predict(xmap_y_dt)
assert xmap_y_p_dt !=. if (_n>1 & _n < _N)
assert xmap_y_p_dt == xmap_y_dt if xmap_y_dt !=. & xmap_y_p_dt != .

edm explore x, predict(predicted_x_dt) copredict(predicted_x_copy_dt) copredictvar(x_copy) full

assert predicted_x_dt == predicted_x_copy_dt if predicted_x_dt!=.

restore

edm explore x, copredict(dx2) copredictvar(d.x)

edm explore x, predict(predicted_x) copredict(predicted_y_from_mx) copredictvar(y) full
edm explore y, predict(predicted_y) full
cor f.x f.y predicted_x predicted_y predicted_y_from_mx
cor f.x predicted_x
assert r(rho)>0.99
cor f.y predicted_y
assert r(rho)>0.99
cor f.y predicted_y_from_mx
assert r(rho)>0.7

global EDM_SAVE_INPUTS = ""

* Obtaining CI of the xmap
jackknife: edm xmap x y, e(2)
ereturn display

jackknife: edm explore x, e(2)
ereturn display

* Further tests that were previously in the 'bigger-test.do' script
global EDM_SAVE_INPUTS = "ci-test"

clear
set obs 100

gen t = _n
tsset t

gen double x = 0.2 if _n==1
gen double y = 0.4 if _n==1

* Create a dynamic system
local r_x 3.625
local r_y 3.77
local beta_xy = 0.05
local beta_yx=0.4
local tau = 1
drawnorm double u1 u2
qui {
    forvalues i=2/`=_N' {
        replace x=l.x *(`r_x' *(1-l.x)-`beta_xy'*l.y) in `i'
        replace y=l.y *(`r_y' *(1-l.y)-`beta_yx'*l`tau'.x) in `i'
    }
}

edm explore x, e(2) crossfold(2) k(-1) allowmissing force

edm explore x, e(2) crossfold(10) k(-1) allowmissing force

edm explore x, e(5) extra(d.y) full allowmissing

// Test e-varying extra is the same as specifying the individual lagged extras
edm explore x, e(5) extra(y(e)) full allowmissing
edm explore x, e(5) extra(L(0/4).y) full allowmissing

edm explore x, e(2 3 4) extra(y(e)) full allowmissing
edm explore x, e(2) extra(L(0/1).y) full allowmissing
edm explore x, e(3) extra(L(0/2).y) full allowmissing
edm explore x, e(4) extra(L(0/3).y) full allowmissing


edm explore x, e(5) extra(y) full

edm explore x, e(5) extra(z.y) full

* identify optimal E
edm explore x, e(2/10)

edm xmap x y, e(10) k(5)

edm xmap x y, theta(0.2) algorithm(smap) savesmap(beta)

ds, detail
format beta* %3.0g
list beta*
drop beta*

edm xmap x y, theta(0.2) algorithm(smap) savesmap(beta) tau(5)

format beta* %3.0g
list beta*
drop beta*

// Introduce missing data and test all the dt variations
set seed 12345678
gen double u = runiform()
drop if u<0.1
replace x = . if u<0.2
replace t = . if mod(t,19) ==1
replace u1 = . if mod(t,7) ==1

edm explore x, copredict(predicted_y_from_mx) copredictvar(y) full
edm explore x, copredict(predicted_y_from_mx_mata) copredictvar(y) full mata

gen err = abs(predicted_y_from_mx - predicted_y_from_mx_mata)
qui replace err = 0 if err < 1e-6
sum err
drop err

format predicted_y_from_mx %8.0g
list predicted_y_from_mx
sum predicted_y_from_mx
drop predicted_y_from_mx
drop predicted_y_from_mx_mata

edm explore x, e(3) extra(u1) dt

edm explore l.x, allowmissing predict(pred)

format pred %8.0g
list pred
drop pred

edm xmap x l.x, oneway allowmissing predict(pred)

format pred %8.0g
list pred
drop pred


edm xmap x y, oneway allowmissing dt dtweight(1) predict(pred)

format pred %8.0g
list pred
drop pred


edm xmap x y, e(3) extra(u1) dt alg(smap) savesmap(beta)

ds, detail
format beta* %3.0g
list beta*
drop beta*

edm xmap x y, e(3) extra(u1) allowmissing dt alg(smap) savesmap(beta)

ds, detail
format beta* %3.0g
list beta*
drop beta*


edm xmap x y, e(3) extra(u1) allowmissing dt alg(smap) savesmap(beta) dtweight(10.0)

ds, detail
format beta* %3.0g
list beta*
drop beta*

edm xmap x y, e(4) extra(L(1/3).u1) allowmissing dt alg(smap) savesmap(beta) force

ds, detail
format beta* %3.0g
list beta*
drop beta*

edm xmap x y, e(2) extra(u1(e)) allowmissing dt alg(smap) savesmap(beta)

ds, detail
format beta* %3.0g
list beta*
drop beta*

edm xmap x y, e(3) extra(z.u1(e)) allowmissing dt alg(smap) savesmap(beta)

ds, detail
format beta* %3.0g
list beta*
drop beta*

edm xmap x y, e(3) extra(L5.u1(e)) allowmissing dt alg(smap) savesmap(beta)

ds, detail
format beta* %3.0g
list beta*
drop beta*

edm xmap x y, e(3) extra(z.L5.u1(e)) allowmissing dt alg(smap) savesmap(beta)

ds, detail
format beta* %3.0g
list beta*
drop beta*

// Check that [dtsave] works on both mata & plugin
edm xmap x y, e(3) extra(u1) allowmissing dt alg(smap) oneway dtsave(plugin)
edm xmap x y, e(3) extra(u1) allowmissing dt alg(smap) oneway dtsave(mata) mata

gen err = plugin != mata
sum err

cap drop plugin
cap drop mata
cap drop err

cap drop err
set seed 1
edm explore x, e(3) extra(u1) dt alg(smap) dtsave(pluginnomissing) dtweight(1) predict(predplugin)
set seed 1
edm explore x, e(3) extra(u1) dt alg(smap) dtsave(matanomissing) mata dtweight(1) predict(predmata)

gen dterr = pluginnomissing != matanomissing
sum dterr

cap drop pluginnomissing
cap drop matanomissing
cap drop dterr

gen preddiff = abs(predplugin - predmata)
qui replace preddiff = 0 if preddiff < 1e-6
sum preddiff

cap drop predplugin
cap drop predmata
cap drop prederr


edm explore x, e(3) extra(u1) allowmissing dt alg(smap) dtsave(plugin)
edm explore x, e(3) extra(u1) allowmissing dt alg(smap) dtsave(mata) mata

gen err = plugin != mata
sum err

capture drop plugin mata err

// Make sure theta changes are reflected, even when k(-1) is specified.
edm explore x, k(-1) theta(0(0.1)2.0) algorithm(smap) full force


// Check that factor variables don't crash everything

edm explore x, extra(i.y)
edm xmap x y, extra(i.y)
edm explore x, e(2/4) extra(x i.y(e) l.x)

// Having factor variables like the following probably doesn't make sense;
// for now, just check that nothing crashes with this kind of command
edm explore i.x
edm xmap i.x i.y
edm xmap i.x y
edm xmap y i.x

// Make sure multiple library values are respected
edm xmap x y, allowmissing dt library(10(5)70)