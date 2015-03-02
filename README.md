# check_tycon
Nagios plugin for checking Tycon Power Monitors

Usage:

	check_tycon -h [hostname] -u [username] -p [password] -c [checkstring]

where `checkstring` is a string of the form `key:min:max`. The `key` can be
any of `volt[1-4], amp[1-4], temp[1-2]`, and `min,max` are floating point
numbers, which will be substituted by the appropriate infinity if left blank.
