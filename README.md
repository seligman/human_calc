Human Calc is a command line app meant to allow free 
form input of formulas and provide useful results.  To 
run it in its most basic form, just run the main script:

```
$ ./human_calc.py
```

Once run, it waits for input, at present here’s some examples 
of what you can do:

```
# Basic math support, including order of operations and parenthesis
42 + 8
= 50
100 + 10 * 5
= 150
(100 + 10) * 5
= 550

# Manual and automatic conversions from similar types
1km + 2500m
= 3.5km
212f in c
= 100C
$100 in CAD
= 125.06 Canadian dollar

# Conversions from compound types
53 gb per 1.5 hours as mb/s
= 10.05037mb/s
53 gb in 1.5 hours as mb/s
= 53gb in 1.5 hours as mb/s

# Operations on the previous result
1 + 20
= 21
+ 300
= 321

# Variable declaration and usage
weight: 100lbs
= 100 pounds
weight / 2
= 50 pounds
```