# $FreeBSD: src/tools/regression/fstest/tests/truncate/07.t,v 1.1 2007/01/17 01:42:12 pjd Exp $

desc="truncate returns ELOOP if too many symbolic links were encountered in translating the pathname"

n0=`namegen`
n1=`namegen`

expect 0 symlink ${n0} ${n1}
expect 0 symlink ${n1} ${n0}
expect ELOOP truncate ${n0}/test 123
expect ELOOP truncate ${n1}/test 123
expect 0 unlink ${n0}
expect 0 unlink ${n1}
