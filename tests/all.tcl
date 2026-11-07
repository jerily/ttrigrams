package require tcltest 2
package require ttrigrams 0.1.0
namespace import ::tcltest::test

test search-1 {Literal and typo matches return document IDs and unique query coverage} -body {
    set index [::ttrigrams::create docs {{web server} {encryption toolkit} aaaaaa}]
    set result [list [$index search web 0] [$index search encrytion 2] [$index search aaaa 0]]
    $index destroy
    set result
} -result {{{document 0 distance 0 shared_trigrams 1 query_trigrams 1}} {{document 1 distance 1 shared_trigrams 5 query_trigrams 7}} {{document 2 distance 0 shared_trigrams 1 query_trigrams 1}}}

test search-2 {Short queries are literal and case folding belongs to callers} -body {
    ::ttrigrams::create docs {tk TCL tcl {}}
    set result [list [docs search tk] [docs search tc 0] [docs search TK] [docs search unknown]]
    docs destroy
    set result
} -result {{{document 0 distance 0 shared_trigrams 0 query_trigrams 0}} {{document 2 distance 0 shared_trigrams 0 query_trigrams 0}} {} {}}

test validation-1 {Validate input and avoid replacing existing commands} -body {
    ::ttrigrams::create docs {}
    set result {}
    foreach command [list {::ttrigrams::create docs {}} [list ::ttrigrams::create bad [list [string repeat a 4097]]] \
        [list ::ttrigrams::create bad [list "a\u0000b"]] {docs search {}} {docs search web -1} {docs search web 3} \
        {docs search web nope} [list docs search [string repeat a 256]] {docs search} {docs destroy extra}] {
        lappend result [catch $command]
    }
    lappend result [expr {![llength [info commands bad]]}]
    docs destroy
    lsort -unique $result
} -result 1

test lifecycle-1 {Renaming and interpreter teardown release indexes} -body {
    set result [namespace eval sample {
        set handle [::ttrigrams::create docs {{web server}}]
        rename $handle renamed
        set count [llength [renamed search web]]
        renamed destroy
        set count
    }]
    namespace delete sample
    interp create child
    child eval [list set auto_path $auto_path]
    child eval {package require ttrigrams; ::ttrigrams::create docs {{a description}}}
    interp delete child
    set result
} -result 1

proc grams {text} {
    set result {}
    for {set i 0} {$i + 2 < [string length $text]} {incr i} {lappend result [string range $text $i [expr {$i+2}]]}
    lsort -unique $result
}
proc words {prefix depth} {
    set result [list $prefix]
    if {$depth} {foreach char {a b} {lappend result {*}[words $prefix$char [expr {$depth-1}]]}}
    return $result
}
proc edit_distance {a b} {
    for {set i 0} {$i <= [string length $a]} {incr i} {set d($i,0) $i}
    for {set j 0} {$j <= [string length $b]} {incr j} {set d(0,$j) $j}
    for {set i 1} {$i <= [string length $a]} {incr i} {
        set pi [expr {$i-1}]
        for {set j 1} {$j <= [string length $b]} {incr j} {
            set pj [expr {$j-1}]
            set cost [expr {[string index $a $pi] ne [string index $b $pj]}]
            set d($i,$j) [expr {min($d($pi,$j)+1,$d($i,$pj)+1,$d($pi,$pj)+$cost)}]
        }
    }
    return $d([string length $a],[string length $b])
}
test reference-1 {Compare candidates and distances with brute-force substring enumeration} -body {
    set texts [words {} 4]
    ::ttrigrams::create original $texts
    ::ttrigrams::load docs [original serialize]
    original destroy
    foreach query [lrange [words {} 4] 1 end] {
        set qgrams [grams $query]
        foreach limit {0 1 2} {
            set expected {}
            set id -1
            foreach text $texts {
                incr id
                set shared 0
                set dgrams [grams $text]
                foreach gram $qgrams {if {$gram in $dgrams} {incr shared}}
                if {[llength $qgrams] && !$shared} {continue}
                set best 99
                if {[string first $query $text] >= 0} {set best 0
                } elseif {[llength $qgrams]} {
                    for {set start 0} {$start < [string length $text]} {incr start} {
                        for {set end $start} {$end < [string length $text]} {incr end} {
                            set best [expr {min($best,[edit_distance $query [string range $text $start $end]])}]
                        }
                    }
                }
                if {$best <= $limit} {
                    lappend expected [list document $id distance $best shared_trigrams $shared query_trigrams [llength $qgrams]]
                }
            }
            if {[docs search $query $limit] ne $expected} {error "Mismatch for $query / $limit"}
        }
    }
    docs destroy
    return ok
} -result ok


test snapshot-1 {Portable format has deterministic bytes} -body {
    ::ttrigrams::create original {abc}
    set hex [binary encode hex [original serialize]]
    original destroy
    set hex
} -result 545452494700010000000001000000010000000361626361626300000000

test snapshot-2 {Binary file round trip preserves searches and survives producer destruction} -body {
    ::ttrigrams::create original {{web server} {encryption toolkit} {} café {web server}}
    set expected {}
    foreach query {tcl web encrytion café unknown} {lappend expected [original search $query]}
    set bytes [original serialize]
    original destroy
    set channel [open [file join $::tcltest::temporaryDirectory index.bin] wb]
    puts -nonewline $channel $bytes
    close $channel
    set channel [open [file join $::tcltest::temporaryDirectory index.bin] rb]
    set restored [::ttrigrams::load restored [read $channel]]
    close $channel
    file delete [file join $::tcltest::temporaryDirectory index.bin]
    set actual {}
    foreach query {tcl web encrytion café unknown} {lappend actual [$restored search $query]}
    set result [list [expr {$expected eq $actual}] [expr {$bytes eq [$restored serialize]}]]
    $restored destroy
    set result
} -result {1 1}

test snapshot-3 {Invalid data and command collisions do not publish or replace commands} -body {
    ::ttrigrams::create original {abc}
    set bytes [original serialize]
    set rejected {}
    foreach invalid [list {} [string range $bytes 0 5] [string range $bytes 0 end-1] \
            [string replace $bytes 6 6 [binary format c 2]] $bytes\x00] {
        lappend rejected [catch {::ttrigrams::load bad $invalid}]
    }
    lappend rejected [catch {::ttrigrams::load original $bytes}] \
        [catch {original serialize extra}] [expr {[info commands bad] eq {}}]
    original destroy
    lsort -unique $rejected
} -result 1

test snapshot-4 {Empty indexes round trip} -body {
    ::ttrigrams::create original {}
    ::ttrigrams::load restored [original serialize]
    set result [restored search web]
    original destroy
    restored destroy
    set result
} -result {}

test snapshot-5 {Document counts include empty and duplicate texts} -body {
    ::ttrigrams::create original {{} abc abc}
    ::ttrigrams::load restored [original serialize]
    set result [list [original count] [restored count]]
    original destroy
    restored destroy
    set result
} -result {3 3}

test snapshot-boundary {Maximum input length survives serialization} -body {
    set value [string repeat a 4096]
    ::ttrigrams::create original [list $value]
    ::ttrigrams::load restored [original serialize]
    set result [expr {[llength [restored search aaa 0]] == 1}]
    original destroy
    restored destroy
    set result
} -result 1

set failed $::tcltest::numTests(Failed)
::tcltest::cleanupTests
exit [expr {$failed != 0}]
