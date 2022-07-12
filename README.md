Human Calc is a command line app meant to allow free 
form input of formulas and provide useful results.  To 
run it in its most basic form, just run the main script:

<pre>
$ ./human_calc.py
</pre>

Once run, it waits for input, at present here’s some examples 
of what you can do:

<pre>
# Basic math support, including order of operations and parenthesis
42 + 8
= 50
100 + 10 * 5
= 150
(100 + 10) * 5
= 550
5 thousand - 1
= 4,999
three hundred * twenty
= 6,000
8 / 4 (2 + 1)
= 6

# Manual and automatic conversions from similar types
1km + 2500m
= 3.5km
212f in c
= 100°C
$100 in EUR
= &#8364;86.18

# Conversions from compound types
53 gb in 1.5 hours as mb/s
= 10.05037mb/s

# Operations on the previous result
1 + 20
= 21
+ 300
= 321
last in English
= Three hundred twenty-one

# Variable declaration and usage
lunch: $25 + $30
= $55.00
tip = 20%
= 0.2
lunch * tip
= $11.00
+ lunch
= $66.00

# Date calculations
2021-01-01 + 7 days
= 2021-01-08
2021-06-01 - 2021-01-01
= 5 months
</pre>
