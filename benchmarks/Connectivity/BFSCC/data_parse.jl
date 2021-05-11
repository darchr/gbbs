mapleaves(f, x...) = f(x...)

function mapleaves(f, x::T...) where {T <: Union{Tuple, AbstractArray}}
    return map((y...) -> mapleaves(f, y...), x...)
end

## Graph 1: Memory A/U over clock ticks
##
## Grab from BFS to BFS (Want neighbor_map())
## - Get Bytes/Read between
## - Get Ratio of Read/Written over used
## - Get relative ticks
## - Get function Names

const EXPERIMENT_PATH =
    Ref(
        expanduser(
            "~/projects/Dungeon/experiments/GBBSTest/results/accesses"
        )
    )

## HACKY SOLUTION RIGHT NOW. WANT TO AUTOMATE LATER!!!
graph       = "livejournal"
kernel      = "cc"
access_file = "livejournal-cc.txt"

map_file     = joinpath(EXPERIMENT_PATH[], graph, graph * "-" * kernel * "-map.txt")
byte_file    = joinpath(EXPERIMENT_PATH[], graph, graph * "-" * kernel * "-byte.txt")
convert_file = joinpath(EXPERIMENT_PATH[], graph, graph * "-" * kernel * "-convert.txt")

#---------------------------------
###
### Preprocess file for plotting
###
#---------------------------------
open(access_file, "r") do io
    data = []

    for line in eachline(io)
        ## Phases
        m = match(r"^BEGIN: ([^,]*).*BytesRead=([^,]*),BytesWritten=([^,]*),ClockTicks=([^,]*)$",line)
        if m != nothing
            push!(data, (m.captures[1], m.captures[2], m.captures[3], m.captures[4]))
            continue
        end

        m = match(r"^END: ([^,]*).*BytesRead=([^,]*),BytesWritten=([^,]*),ClockTicks=([^,]*)$",line)

        if m != nothing
            push!(data, (m.captures[1], m.captures[2], m.captures[3], m.captures[4]))
            continue
        end

        m = match(r"^CONVERT: ([^,]*).*ClockTicks=([^,]*)$",line)
        if m != nothing
            push!(data, (m.captures[1], m.captures[2]))
            continue
        end

        m = match(r"^(Memory[^,]*).*,MemSize=([^,]*),Used=([^,]*),Allocated=([^,]*),BytesRead=([^,]*),BytesWritten=([^,]*),ClockTicks=([^,]*)$",line)

        if m != nothing
            push!(
                data,
                (
                    m.captures[1],
                    m.captures[2],
                    m.captures[3],
                    m.captures[4],
                    m.captures[5],
                    m.captures[6],
                    m.captures[7]
                )
            )
        end
    end

    ## Grab neighbor map points, used mem change points, read/write total sections

    ## Remove graph memory setup
    mem           = []
    bytes         = []
    maps          = []
    switch        = []
    start_tick    = parse(Float64, data[1][4]) ## Only thing that changes (between kernels)
    read_total    = 0
    written_total = 0

    #push!(
    #    mem,
    #    (
    #        parse(Float64, data[2][3]),
    #        parse(Int, data[2][5]),
    #        parse(Int, data[2][6]),
    #        0
    #    )
    #)
    push!(bytes, (0,0,0,data[1][1]))                                                         

    data = data[2:length(data)]

    end_tick      = parse(Float64, data[length(data)][4]) - start_tick
    begin_switch  = false
    begin_seen    = false

    ## Build graphs mem(3,5,6,7) and rest(2,3,4)
    for results in data
        if results[1] == "edgeMap() Init" || results[1] == "vertexMap()"
            push!(
                bytes,
                (
                    parse(Int, results[2]),
                    parse(Int, results[3]),
                    parse(Float64, results[4]),
                    results[1]
                    #(parse(Float64, results[4]) - start_tick) / end_tick
                )
            )
            if begin_seen
                begin_seen = false

                push!(
                    maps,
                    (
                        #read_total + parse(Int, results[2]),
                        #written_total + parse(Int, results[3]),
                        parse(Int, results[2]),
                        parse(Int, results[3]),
                        parse(Float64, results[4]),
                        results[1]
                        #(parse(Float64, results[4]) - start_tick) / end_tick
                    )
                )
                #read_total    = 0
                #written_total = 0
            else
                begin_seen    = true
                read_total    = 0
                written_total = 0

                push!(
                    maps,
                    (
                        #0,
                        #0,
                        parse(Int, results[2]),
                        parse(Int, results[3]),
                        parse(Float64, results[4]),
                        results[1]
                        #(parse(Float64, results[4]) - start_tick) / end_tick
                    )
                )
            end
        elseif results[1] == "MemoryBucketAlloc" || results[1] == "MemoryBucketFree"
            read_total    += parse(Int, results[5])
            written_total += parse(Int, results[6])

            push!(
                bytes,
                (
                    parse(Int, results[5]),
                    parse(Int, results[6]),
                    parse(Float64, results[7]),
                    results[1]
                    #(parse(Float64, results[7]) - start_tick) / end_tick
                )
            )
            push!(
                mem,
                (
                    parse(Int, results[3]),
                    parse(Int, results[5]),
                    parse(Int, results[6]),
                    parse(Float64, results[7])
                    #(parse(Float64, results[7]) - start_tick) / end_tick
                )
            )
        elseif results[1] == "toSparse()" || results[1] == "toDense()"
            #read_total    += parse(Int, results[2])
            #written_total += parse(Int, results[3])

            push!(
                switch,
                (
                    results[1],
                    begin_switch ? "END" : "BEGIN",
                    parse(Float64, results[2])
                    #(parse(Float64, results[4]) - start_tick) / end_tick
                )
            )
            begin_switch = !begin_switch
        else
            read_total    += parse(Int, results[2])
            written_total += parse(Int, results[3])

            push!(
                bytes,
                (
                    parse(Int, results[2]),
                    parse(Int, results[3]),
                    parse(Float64, results[4]),
                    results[1]
                    #(parse(Float64, results[4]) - start_tick) / end_tick
                )
            )

        end
    end

##    open("mem.txt", "w") do dest
##        ## Add Header
##        write(dest, "Used")
##        write(dest, ",")
##        write(dest, "RatioRead")
##        write(dest, ",")
##        write(dest, "RatioWritten")
##        write(dest, ",")
##        write(dest, "Time")
##        write(dest, "\n")
##
##        for vals in mem
##            write(dest, string(vals[1]))
##            write(dest, ",")
##            write(dest, string(vals[2]/vals[1]))
##            write(dest, ",")
##            write(dest, string(vals[3]/vals[1]))
##            write(dest, ",")
##            write(dest, string(vals[4]))
##            write(dest, "\n")
##        end
##    end
    open(byte_file, "w") do dest
        ## Add Header
        write(dest, "Access")
        write(dest, ",")
        write(dest, "Bandwidth")
        write(dest, ",")
        write(dest, "Time")
        write(dest, ",")
        write(dest, "Method")
        write(dest, "\n")

        for vals in bytes
            write(dest, "Read")
            write(dest, ",")
            write(dest, string(vals[1]/vals[3]))
            write(dest, ",")
            write(dest, string(vals[3]))
            write(dest, ",")
            write(dest, vals[4])
            write(dest, "\n")
            write(dest, "Write")
            write(dest, ",")
            write(dest, string(vals[2]/vals[3]))
            write(dest, ",")
            write(dest, string(vals[3]))
            write(dest, ",")
            write(dest, vals[4])
            write(dest, "\n")
        end
    end
    open(map_file, "w") do dest
        ## Add Header
        write(dest, "Access")
        write(dest, ",")
        write(dest, "Bandwidth")
        write(dest, ",")
        write(dest, "Time")
        write(dest, ",")
        write(dest, "Method")
        write(dest, "\n")

        for vals in maps
            write(dest, "Read")
            write(dest, ",")
            write(dest, string(vals[1]/vals[3]))
            write(dest, ",")
            write(dest, string(vals[3]))
            write(dest, ",")
            write(dest, vals[4])
            write(dest, "\n")
            write(dest, "Write")
            write(dest, ",")
            write(dest, string(vals[2]/vals[3]))
            write(dest, ",")
            write(dest, string(vals[3]))
            write(dest, ",")
            write(dest, vals[4])
            write(dest, "\n")
        end
    end
    open(convert_file, "w") do dest
        ## Add Header
        write(dest, "Method")
        write(dest, ",")
        write(dest, "Position")
        write(dest, ",")
        write(dest, "Time")
        write(dest, "\n")

        for vals in switch
            write(dest, string(vals[1]))
            write(dest, ",")
            write(dest, vals[2])
            write(dest, ",")
            write(dest, string(vals[3]))
            write(dest, "\n")
        end
    end
end

## Graph 2: Memory addresses over ticks
##
## Grab from BFS to BFS (Want neighbor_map())
## - Get Alloc/used
## - Get memory addresses
